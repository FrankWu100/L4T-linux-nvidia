/*
 * NVDLA queue and task management for T194
 *
 * Copyright (c) 2016, NVIDIA Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#include "dev.h"
#include "bus_client.h"
#include "chip_support.h"
#include "nvhost_acm.h"
#include "nvhost_queue.h"
#include "nvhost_syncpt_unit_interface.h"

#include "nvdla/nvdla.h"
#include "nvdla/nvdla_debug.h"
#include <linux/nvhost_nvdla_ioctl.h>
#include "dla_os_interface.h"

/* TODO: 1. revisit timeout post silicon
 *       2. when silicon and sim tests go live at same time,
 *          make timeout selection runtime based on platform
 */
#define NVDLA_QUEUE_ABORT_TIMEOUT	10000	/* 10 sec */
#define NVDLA_QUEUE_ABORT_RETRY_PERIOD	500	/* 500 ms */

static DEFINE_DMA_ATTRS(attrs);

/* task management API's */
static int nvdla_get_fences(struct nvdla_ioctl_submit_task *user_task,
			struct nvdla_task *task)
{
	struct nvdla_fence __user *postfences =
		(struct nvdla_fence __user *)(uintptr_t)user_task->postfences;
	struct nvdla_fence __user *prefences =
		(struct nvdla_fence __user *)(uintptr_t)user_task->prefences;
	u32 num_postfences = user_task->num_postfences;
	u32 num_prefences = user_task->num_prefences;
	struct nvdla_fence fence;
	int err = 0;
	u32 i = 0;

	/* get pre fences */
	for (i = 0; i < num_prefences; i++, prefences++) {
		err = copy_from_user(&fence, prefences,
			sizeof(struct nvdla_fence));
		if (err)
			goto fail;

		if (fence.syncpoint_index == 0)
			goto fail;

		task->prefences[i].fence_type = fence.type;
		task->prefences[i].id = fence.syncpoint_index;
		task->prefences[i].val = fence.syncpoint_value;
	}

	/* get post fences */
	for (i = 0; i < num_postfences; i++, postfences++) {
		err = copy_from_user(&fence, postfences,
			sizeof(struct nvdla_fence));
		if (err)
			goto fail;

		if (fence.syncpoint_index == 0)
			goto fail;

		task->postfences[i].fence_type = fence.type;
	}
fail:
	return err;
}

int nvdla_send_postfences(struct nvdla_task *task,
			struct nvdla_ioctl_submit_task usr_task)
{
	struct nvdla_fence __user *postfences =
		(struct nvdla_fence __user *)(uintptr_t)usr_task.postfences;
	u32 num_postfences = usr_task.num_postfences;
	struct nvdla_fence fence;
	int err = 0;
	int i;

	/* send post fences */
	for (i = 0; i < num_postfences; i++, postfences++) {
		fence.syncpoint_index = task->postfences[i].id;
		fence.syncpoint_value = task->postfences[i].fence;
		fence.type = task->postfences[i].fence_type;

		err = copy_to_user(postfences, &fence,
				sizeof(struct nvdla_fence));
		if (err)
			goto fail;
	}
fail:
	return err;
}

static void task_free(struct kref *ref)
{
	struct nvdla_task *task = container_of(ref, struct nvdla_task, ref);
	struct platform_device *pdev = task->queue->pool->pdev;

	nvdla_dbg_info(pdev, "freeing task[%p]", task);

	/* free allocated task desc */
	if (task->task_desc) {
		dma_free_attrs(&pdev->dev, task->buf_size,
			task->task_desc, task->task_desc_pa,
			&attrs);
		task->task_desc = NULL;
	}

	/* free operation descriptor handle */
	if (task->memory_handles)
		kfree(task->memory_handles);

	/* finally free task */
	kfree(task);
}

void nvdla_task_put(struct nvdla_task *task)
{
	/* release queue refcnt */
	nvhost_queue_put(task->queue);

	kref_put(&task->ref, task_free);
}

void nvdla_task_get(struct nvdla_task *task)
{
	kref_get(&task->ref);

	/* update queue refcnt */
	nvhost_queue_get(task->queue);
}

static void nvdla_task_free_locked(struct nvdla_task *task)
{
	struct nvhost_queue *queue = task->queue;
	struct platform_device *pdev = queue->pool->pdev;

	nvdla_dbg_info(pdev,
		"task[%p] completed. syncpt[%d] fence[%d]",
		task, queue->syncpt_id, task->fence);

	/* give syncpoint reference */
	nvhost_syncpt_put_ref(task->sp, queue->syncpt_id);

	/* unpin submit ref */
	if (task->num_handles)
		nvhost_buffer_submit_unpin(task->buffers,
			task->memory_handles, task->num_handles);

	/* update takslist */
	list_del(&task->list);

	/* give taks refs */
	nvdla_task_put(task);
}

static void nvdla_task_syncpt_reset(struct nvhost_syncpt *syncpt,
			u32 id, u32 fence)
{
	atomic_set(&syncpt->min_val[id], fence);
	syncpt_op().reset(syncpt, id);
	nvhost_syncpt_update_min(syncpt, id);
}

static void nvdla_queue_update(void *priv, int nr_completed)
{
	int task_complete;
	struct nvdla_task *task, *safe;
	struct nvhost_queue *queue = priv;
	struct platform_device *pdev = queue->pool->pdev;

	mutex_lock(&queue->list_lock);

	/* check which task(s) finished */
	list_for_each_entry_safe(task, safe, &queue->tasklist, list) {

		task_complete = nvhost_syncpt_is_expired(task->sp,
					queue->syncpt_id, task->fence);

		/* clean task and remove from list */
		if (task_complete)
			nvdla_task_free_locked(task);
	}
	/* put pm refcount */
	nvhost_module_idle_mult(pdev, nr_completed);

	mutex_unlock(&queue->list_lock);
}

static int nvdla_map_task_memory(struct nvhost_buffers *buffers,
			struct nvdla_ioctl_submit_task *user_task,
			struct nvdla_task *task,
			struct dla_task_descriptor *task_desc)
{
	int i;
	int err = 0;
	u32 *handles;
	size_t *dma_size;
	void *ptr = NULL;
	dma_addr_t *dma_addr;
	dma_addr_t *dma_memory;
	struct dma_buf *buf = NULL;
	struct nvdla_mem_handle *addresses;

	task->buffers = buffers;
	task->num_handles = 0;

	/* keep address list always last */
	if (user_task->num_addresses)
		task->num_handles = user_task->num_addresses + 1;

	if (task->num_handles == 0)
		return err;

	/*
	 * Allocate memory to store information for DMA mapping of
	 * buffers allocated from user space
	 */
	task->memory_handles = kcalloc(task->num_handles, sizeof(u32),
				GFP_KERNEL);
	if (!task->memory_handles) {
		err = -ENOMEM;
		goto fail_to_alloc_handles;
	}

	handles = task->memory_handles;

	dma_addr = kcalloc(task->num_handles, sizeof(dma_addr_t),
				GFP_KERNEL);
	if (!dma_addr) {
		err = -ENOMEM;
		goto fail_to_alloc_dma_addr;
	}

	dma_memory = dma_addr;
	dma_size = kcalloc(task->num_handles, sizeof(u32),
				GFP_KERNEL);
	if (!dma_size) {
		err = -ENOMEM;
		goto fail_to_alloc_dma_size;
	}

	/*
	 * Fill in handles from list of addresses, need to map
	 * address list buffer in kernel and update same buffer
	 * with DMA addresses obtained.
	 */
	if (user_task->num_addresses) {
		uintptr_t temp;

		*handles++ = user_task->address_list.handle;

		buf = dma_buf_get(user_task->address_list.handle);
		if (IS_ERR(buf)) {
			err = PTR_ERR(buf);
			goto fail_to_pin_mem;
		}

		ptr = dma_buf_vmap(buf);
		if (!ptr) {
			err = -ENOMEM;
			goto fail_to_pin_mem;
		}

		dma_buf_begin_cpu_access(buf, user_task->address_list.offset,
				sizeof(uint64_t) * user_task->num_addresses,
				DMA_TO_DEVICE);

		temp = (uintptr_t)(ptr);
		addresses =
			(struct nvdla_mem_handle *)
				(temp + user_task->address_list.offset);

		for (i = 0; i < user_task->num_addresses; i++, addresses++)
			*handles++ = addresses->handle;
	}

	/* Get DMA addresses for all handles */
	err = nvhost_buffer_submit_pin(buffers, task->memory_handles,
				task->num_handles, dma_addr, dma_size);
	if (err) {
		goto fail_to_pin_mem;
	}

	/* Update IOVA addresses in task descriptor */
	task_desc->num_addresses = user_task->num_addresses;
	if (user_task->num_addresses) {
		uintptr_t temp;
		uint64_t *dma_addr_list;

		temp = (uintptr_t)(ptr);
		dma_addr_list = (uint64_t *)
				(temp + user_task->address_list.offset);
		addresses =
			(struct nvdla_mem_handle *)
				(temp + user_task->address_list.offset);

		task_desc->address_list = (*dma_addr++) +
					user_task->address_list.offset;

		for (i = 0; i < user_task->num_addresses; i++, addresses++) {
			uint64_t offset = (uint64_t)addresses->offset;

			*dma_addr_list++ = (uint64_t)(*dma_addr++) + offset;
		}

		dma_buf_vunmap(buf, ptr);

		dma_buf_end_cpu_access(buf, user_task->address_list.offset,
				sizeof(uint64_t) * user_task->num_addresses,
				DMA_TO_DEVICE);

		dma_buf_put(buf);
	}

	if (dma_memory)
		kfree(dma_memory);
	if (dma_size)
		kfree(dma_size);

	return 0;

fail_to_pin_mem:
	if (dma_size)
		kfree(dma_size);
fail_to_alloc_dma_size:
	if (dma_memory)
		kfree(dma_memory);
fail_to_alloc_dma_addr:
	if (task->memory_handles)
		kfree(task->memory_handles);
fail_to_alloc_handles:
	return err;
}

struct nvdla_task *nvdla_task_alloc(struct nvhost_queue *queue,
			struct nvhost_buffers *buffers,
			struct nvdla_ioctl_submit_task *user_task)
{
	struct platform_device *pdev = queue->pool->pdev;
	u32 num_postfences = user_task->num_postfences;
	u32 num_prefences = user_task->num_prefences;
	struct dla_action_semaphore *postaction;
	struct dla_action_semaphore *preaction;
	struct dla_task_descriptor *task_desc;
	struct dla_action_list *postactionl;
	struct dla_action_list *preactionl;
	struct dla_action_opcode *opcode;
	struct nvdla_task *task = NULL;
	uint16_t postactionlist_of;
	size_t postactionlist_size;
	uint16_t preactionlist_of;
	size_t preactionlist_size;
	uint16_t postactionl_of;
	uint16_t preactionl_of;
	dma_addr_t buffer_pa;
	size_t task_size;
	size_t buf_size;
	u32 *buffer_va;
	void *mem;
	int err;
	int i;

	nvdla_dbg_fn(pdev, "");

	/* allocate task resource */
	task_size = sizeof(struct nvdla_task) +
			(num_prefences + num_postfences) *
			sizeof(struct nvdla_task_fence);

	task = kzalloc(task_size, GFP_KERNEL);
	if (!task) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "task allocation failed");
		goto fail_to_alloc_task;
	}

	/* initialize task parameters */
	kref_init(&task->ref);
	task->queue = queue;
	task->sp = &nvhost_get_host(pdev)->syncpt;

	/* assign memory for local pre and post action lists */
	mem = task;
	mem += sizeof(struct nvdla_task);
	task->prefences = mem;
	mem += num_prefences * sizeof(struct nvdla_task_fence);
	task->postfences = mem;

	/* update local fences into task*/
	nvdla_get_fences(user_task, task);

	/* calculate size of task desc, actions and its list, buffers
	 * this is max possible size for updating task desc and
	 * and allocated mem size can be more than required size
	 */
	preactionlist_size =
			num_prefences * sizeof(struct dla_action_opcode) +
			num_prefences * sizeof(struct dla_action_semaphore) +
			sizeof(struct dla_action_opcode);

	postactionlist_size =
			num_postfences * sizeof(struct dla_action_opcode) +
			num_postfences * sizeof(struct dla_action_semaphore) +
			sizeof(struct dla_action_opcode);

	buf_size = sizeof(struct dla_task_descriptor) +
		(2 * MAX_NUM_ACTION_LIST * sizeof(struct dla_action_list)) +
		preactionlist_size +
		postactionlist_size;

	nvdla_dbg_info(pdev, "num of prefences[%d] num of postfences[%d]",
			num_prefences, num_postfences);
	nvdla_dbg_info(pdev, "preaction list size[%zu]",
			preactionlist_size);
	nvdla_dbg_info(pdev, "postaction list size[%zu]",
			postactionlist_size);
	nvdla_dbg_info(pdev, "Total task desc size[%zu]", buf_size);

	/* allocate task descriptor */
	buffer_va = dma_alloc_attrs(&pdev->dev, buf_size, &buffer_pa,
				GFP_KERNEL, &attrs);

	if (!buffer_va) {
		dev_err(&pdev->dev, "dma memory allocation failed for task");
		err = -ENOMEM;
		goto fail_to_dma_alloc;
	}

	task->task_desc = (struct dla_task_descriptor *)(buffer_va);
	task_desc = task->task_desc;
	task->task_desc_pa = buffer_pa;
	task->buf_size = buf_size;

	/* update task desc fields */
	task_desc->version = DLA_DESCRIPTOR_VERSION;
	task_desc->engine_id = DLA_ENGINE_ID;
	task_desc->size = buf_size;

	/* update current task sequeue, make sure wrap around condition */
	queue->sequence = queue->sequence + 1;
	if (unlikely(queue->sequence >= (UINT_MAX - 1)))
		queue->sequence = 0;

	task_desc->sequence = queue->sequence;

	/* below are actual number of action lists
	 * DLA has one preaction list and one postaction list
	 */
	task_desc->num_preactions = MAX_NUM_ACTION_LIST;
	task_desc->num_postactions = MAX_NUM_ACTION_LIST;

	task_desc->queue_id = queue->id;

	nvdla_dbg_info(pdev, "Queue id[%d]", task_desc->queue_id);

	/* get pre/post action list HEAD mem offset
	 * - preactions list HEAD stored after dla_task_descriptor
	 * - postactions list HEAD followed after preaction list head offset
	 * - DLA has only one list of actions for each of pre and post
	 */
	preactionl_of = sizeof(struct dla_task_descriptor);
	postactionl_of = preactionl_of + sizeof(struct dla_action_list);

	nvdla_dbg_info(pdev, "preaction meta offset[%d]", preactionl_of);
	nvdla_dbg_info(pdev, "postaction meta offset[%d]", postactionl_of);

	/* ..and send those through descriptor */
	task_desc->preactions = preactionl_of;
	task_desc->postactions = postactionl_of;

	/* actual preaction list offset update */
	preactionlist_of = postactionl_of + sizeof(struct dla_action_list);

	/* actual postaction list offset update */
	postactionlist_of = preactionlist_of + preactionlist_size;

	nvdla_dbg_info(pdev, "preaction list offset[%d]", preactionlist_of);
	nvdla_dbg_info(pdev, "postaction list offset[%d]", postactionlist_of);

	/* actually update lists data */
	mem = (char *)task_desc + preactionl_of;
	preactionl = (struct dla_action_list *)mem;
	preactionl->offset = preactionlist_of;
	preactionl->size = preactionlist_size;

	mem = (char *)task_desc + postactionl_of;
	postactionl = (struct dla_action_list *)mem;
	postactionl->offset = postactionlist_of;
	postactionl->size = postactionlist_size;

	/* fill all preactions */
	for (i = 0; i <= user_task->num_prefences; i++) {
		void *next = NULL;

		/* get next preaction base */
		next = (char *)task_desc + preactionlist_of +
		  i * (sizeof(struct dla_action_opcode) +
			sizeof(struct dla_action_semaphore));

		/* get base opcode */
		opcode = (struct dla_action_opcode *)next;

		/* update end of action list */
		if (i == user_task->num_prefences) {
			opcode->value = PREACTION_TERMINATE;
			break;
		}

		/* set action type */
		opcode->value = PREACTION_SEM_GE;

		/* get actual preaction address */
		preaction = (struct dla_action_semaphore *)
			((char *)opcode + sizeof(struct dla_action_opcode));

		/* update action */
		preaction->address = nvhost_syncpt_address(pdev, task->prefences[i].id);
		preaction->value = task->prefences[i].val;
	}

	/* fill all postactions */
	for (i = 0; i < user_task->num_postfences; i++, postaction++) {
		void *next = NULL;

		/* get next post action base */
		next = (char *)task_desc + postactionlist_of +
		 i * (sizeof(struct dla_action_opcode) +
			sizeof(struct dla_action_semaphore));

		/* get base opcode */
		opcode = (struct dla_action_opcode *)next;

		/* update end of list */
		if (i == user_task->num_postfences) {
			opcode->value = POSTACTION_TERMINATE;
			break;
		}

		/* set action type */
		opcode->value = POSTACTION_SEM;

		/* get actual post action mem */
		postaction = (struct dla_action_semaphore *)
			((char *)opcode + sizeof(struct dla_action_opcode));

		/* update action */
		postaction->address = nvhost_syncpt_address(pdev, queue->syncpt_id);
	}

	err = nvdla_map_task_memory(buffers, user_task, task, task_desc);
	if (err)
		goto fail_to_dma_alloc;

	nvdla_dbg_info(pdev, "task[%p] initialized", task);

	return task;

fail_to_dma_alloc:
	kfree(task);
fail_to_alloc_task:
	return ERR_PTR(err);
}

/* Queue management API */
static int nvdla_queue_submit(struct nvhost_queue *queue, void *in_task)
{
	struct nvdla_task *task = (struct nvdla_task *)in_task;
	struct nvdla_task *last_task = NULL;
	struct platform_device *pdev = queue->pool->pdev;
	uint32_t method_data;
	uint32_t method_id;
	int err = 0;

	nvdla_dbg_fn(pdev, "");

	/* get pm refcount */
	if (nvhost_module_busy(pdev))
		return -EINVAL;

	mutex_lock(&queue->list_lock);

	/* get task ref and add to list */
	nvdla_task_get(task);

	/* update last task desc's next */
	if (!list_empty(&queue->tasklist)) {
		last_task = list_last_entry(&queue->tasklist,
						struct nvdla_task, list);
		last_task->task_desc->next = (uint64_t)task->task_desc_pa;
	}
	list_add_tail(&task->list, &queue->tasklist);

	nvdla_dbg_info(pdev, "task[%p] added to list", task);

	/* get fence from nvhost */
	task->fence = nvhost_syncpt_incr_max(task->sp, queue->syncpt_id, 1);

	nvdla_dbg_fn(pdev, "syncpt[%d] fence[%d] task[%p]", queue->syncpt_id,
				task->fence, task);

	/* get syncpoint reference */
	nvhost_syncpt_get_ref(task->sp, queue->syncpt_id);

	/* enable INT_ON_COMPLETE and INT_ON_ERROR falcon interrupts */
	method_id = (DLA_CMD_SUBMIT_TASK & DLA_METHOD_ID_CMD_MASK) |
			(1 << DLA_INT_ON_COMPLETE_SHIFT) |
			(1 << DLA_INT_ON_ERROR_SHIFT);
	method_data = ((task->task_desc_pa >> 8) & 0xffffffff);

	/* register notifier with fence */
	err = nvhost_intr_register_notifier(pdev, queue->syncpt_id,
		task->fence, nvdla_queue_update, queue);
	if (err)
		goto fail_to_register;

	/* Pass fence as through 0th postfences */
	task->postfences[0].id = queue->syncpt_id;
	task->postfences[0].fence = task->fence;

	/* submit task to engine */
	err = nvdla_send_cmd(pdev, method_id, method_data, true);
	if (err)
		nvdla_task_syncpt_reset(task->sp, queue->syncpt_id, task->fence);

fail_to_register:
	mutex_unlock(&queue->list_lock);

	return err;
}

static int nvdla_queue_abort(struct nvhost_queue *queue)
{
	int err;
	struct nvdla_task *t;
	struct platform_device *pdev = queue->pool->pdev;
	int retry = NVDLA_QUEUE_ABORT_TIMEOUT / NVDLA_QUEUE_ABORT_RETRY_PERIOD;

	nvdla_dbg_fn(pdev, "");

	/* get pm refcount */
	err = nvhost_module_busy(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to poweron, err: %d", err);
		return err;
	}

	/* flush engine side queues */
	do {
		err = nvdla_send_cmd(pdev, DLA_CMD_QUEUE_FLUSH, queue->id,
					true);
		if (err == DLA_ERR_PROCESSOR_BUSY)
			mdelay(NVDLA_QUEUE_ABORT_RETRY_PERIOD);
		else
			break;
	} while (--retry);

	if (!retry || err) {
		nvdla_dbg_err(pdev,
		"Q %d abort fail. err:%d, retry:%d",
			queue->id, err, retry);
		goto done;
	}

	nvdla_dbg_info(pdev, "Engine Q[%d] flush done", queue->id);

	/* if task present free them by reset syncpoint */
	if (!list_empty(&queue->tasklist)) {
		t = list_last_entry(&queue->tasklist, struct nvdla_task, list);

		/* reset syncpoint to release all tasks */
		nvdla_task_syncpt_reset(t->sp, queue->syncpt_id, t->fence);

		/* dump details */
		nvdla_dbg_info(pdev, "Q id %d reset syncpt[%d] done",
			queue->id, queue->syncpt_id);
		nvdla_dbg_info(pdev, "syncpt[%d], min[%u], max[%u]",
			queue->syncpt_id,
			nvhost_syncpt_update_min(t->sp, queue->syncpt_id),
			nvhost_syncpt_read_max(t->sp, queue->syncpt_id));
	}

done:
	nvhost_module_idle(pdev);
	return err;
}

struct nvhost_queue_ops nvdla_queue_ops = {
	.abort = nvdla_queue_abort,
	.submit = nvdla_queue_submit,
};
