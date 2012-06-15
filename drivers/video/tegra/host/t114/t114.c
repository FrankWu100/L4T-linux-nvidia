/*
 * drivers/video/tegra/host/t114/t114.c
 *
 * Tegra Graphics Init for T114 Architecture Chips
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/mutex.h>
#include <mach/powergate.h>
#include <mach/iomap.h>
#include "dev.h"
#include "host1x/host1x_cdma.h"
#include "t20/t20.h"
#include "t30/t30.h"
#include "t114.h"
#include "host1x/host1x_hardware.h"
#include "host1x/host1x_syncpt.h"
#include "host1x/host1x_actmon.h"
#include "gr3d/gr3d.h"
#include "gr3d/gr3d_t114.h"
#include "gr3d/scale3d.h"
#include "msenc/msenc.h"
#include "tsec/tsec.h"
#include <linux/nvhost_ioctl.h>
#include "nvhost_channel.h"
#include "nvhost_memmgr.h"
#include "chip_support.h"

#define NVMODMUTEX_2D_FULL   (1)
#define NVMODMUTEX_2D_SIMPLE (2)
#define NVMODMUTEX_2D_SB_A   (3)
#define NVMODMUTEX_2D_SB_B   (4)
#define NVMODMUTEX_3D        (5)
#define NVMODMUTEX_DISPLAYA  (6)
#define NVMODMUTEX_DISPLAYB  (7)
#define NVMODMUTEX_VI        (8)
#define NVMODMUTEX_DSI       (9)

#define HOST_EMC_FLOOR 300000000

static int t114_num_alloc_channels = 0;

static struct nvhost_device tegra_display01_device = {
	.name	       = "display",
	.id            = -1,
	.index         = 0,
	.syncpts       = BIT(NVSYNCPT_DISP0_A) | BIT(NVSYNCPT_DISP1_A) |
			 BIT(NVSYNCPT_DISP0_B) | BIT(NVSYNCPT_DISP1_B) |
			 BIT(NVSYNCPT_DISP0_C) | BIT(NVSYNCPT_DISP1_C) |
			 BIT(NVSYNCPT_VBLANK0) | BIT(NVSYNCPT_VBLANK1),
	.modulemutexes = BIT(NVMODMUTEX_DISPLAYA) | BIT(NVMODMUTEX_DISPLAYB),
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_NONE,
};

static struct nvhost_device tegra_gr3d03_device = {
	.name	       = "gr3d03",
	.id            = -1,
	.index         = 1,
	.syncpts       = BIT(NVSYNCPT_3D),
	.waitbases     = BIT(NVWAITBASE_3D),
	.modulemutexes = BIT(NVMODMUTEX_3D),
	.class	       = NV_GRAPHICS_3D_CLASS_ID,
	.clocks = {{"gr3d", UINT_MAX},
			{"emc", HOST_EMC_FLOOR} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_NONE,
};

static struct nvhost_device tegra_gr2d03_device = {
	.name	       = "gr2d",
	.id            = -1,
	.index         = 2,
	.syncpts       = BIT(NVSYNCPT_2D_0) | BIT(NVSYNCPT_2D_1),
	.waitbases     = BIT(NVWAITBASE_2D_0) | BIT(NVWAITBASE_2D_1),
	.modulemutexes = BIT(NVMODMUTEX_2D_FULL) | BIT(NVMODMUTEX_2D_SIMPLE) |
			 BIT(NVMODMUTEX_2D_SB_A) | BIT(NVMODMUTEX_2D_SB_B),
	.clocks = {{"gr2d", 0},
			{"epp", UINT_MAX},
			{"emc", HOST_EMC_FLOOR} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_NONE,
};

static struct resource isp_resources[] = {
	{
		.name = "regs",
		.start = TEGRA_ISP_BASE,
		.end = TEGRA_ISP_BASE + TEGRA_ISP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct nvhost_device tegra_isp01_device = {
	.name	 = "isp",
	.id      = -1,
	.resource = isp_resources,
	.num_resources = ARRAY_SIZE(isp_resources),
	.index   = 3,
	.syncpts = 0,
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_ISP,
};

static struct resource vi_resources[] = {
	{
		.name = "regs",
		.start = TEGRA_VI_BASE,
		.end = TEGRA_VI_BASE + TEGRA_VI_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct nvhost_device tegra_vi01_device = {
	.name	       = "vi",
	.id            = -1,
	.resource      = vi_resources,
	.num_resources = ARRAY_SIZE(vi_resources),
	.index         = 4,
	.syncpts       = BIT(NVSYNCPT_CSI_VI_0) | BIT(NVSYNCPT_CSI_VI_1) |
			 BIT(NVSYNCPT_VI_ISP_0) | BIT(NVSYNCPT_VI_ISP_1) |
			 BIT(NVSYNCPT_VI_ISP_2) | BIT(NVSYNCPT_VI_ISP_3) |
			 BIT(NVSYNCPT_VI_ISP_4),
	.modulemutexes = BIT(NVMODMUTEX_VI),
	.exclusive     = true,
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_VI,
};

static struct resource msenc_resources[] = {
	{
		.name = "regs",
		.start = TEGRA_MSENC_BASE,
		.end = TEGRA_MSENC_BASE + TEGRA_MSENC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct nvhost_device tegra_msenc02_device = {
	.name	       = "msenc",
	.id            = -1,
	.resource      = msenc_resources,
	.num_resources = ARRAY_SIZE(msenc_resources),
	.index         = 5,
	.syncpts       = BIT(NVSYNCPT_MSENC),
	.waitbases     = BIT(NVWAITBASE_MSENC),
	.class	       = NV_VIDEO_ENCODE_MSENC_CLASS_ID,
	.exclusive     = false,
	.keepalive     = true,
	.clocks = {{"msenc", UINT_MAX}, {"emc", HOST_EMC_FLOOR} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_MSENC,
};

static struct nvhost_device tegra_dsi01_device = {
	.name	       = "dsi",
	.id            = -1,
	.index         = 6,
	.syncpts       = BIT(NVSYNCPT_DSI),
	.modulemutexes = BIT(NVMODMUTEX_DSI),
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_NONE,
};

static struct resource tsec_resources[] = {
	{
		.name = "regs",
		.start = TEGRA_TSEC_BASE,
		.end = TEGRA_TSEC_BASE + TEGRA_TSEC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct nvhost_device tegra_tsec01_device = {
	/* channel 7 */
	.name          = "tsec",
	.id            = -1,
	.resource      = tsec_resources,
	.num_resources = ARRAY_SIZE(tsec_resources),
	.index         = 7,
	.syncpts       = BIT(NVSYNCPT_TSEC),
	.waitbases     = BIT(NVWAITBASE_TSEC),
	.class         = NV_TSEC_CLASS_ID,
	.exclusive     = false,
	.clocks = {{"tsec", UINT_MAX}, {"emc", HOST_EMC_FLOOR} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
	NVHOST_DEFAULT_CLOCKGATE_DELAY,
	.moduleid      = NVHOST_MODULE_TSEC,
};

static struct nvhost_device *t11_devices[] = {
	&tegra_host1x01_device,
	&tegra_display01_device,
	&tegra_gr3d03_device,
	&tegra_gr2d03_device,
	&tegra_isp01_device,
	&tegra_vi01_device,
	&tegra_msenc02_device,
	&tegra_dsi01_device,
	&tegra_tsec01_device,
};

int tegra11_register_host1x_devices(void)
{
	return nvhost_add_devices(t11_devices, ARRAY_SIZE(t11_devices));
}

static inline int t114_nvhost_hwctx_handler_init(struct nvhost_channel *ch)
{
	int err = 0;
	unsigned long syncpts = ch->dev->syncpts;
	unsigned long waitbases = ch->dev->waitbases;
	u32 syncpt = find_first_bit(&syncpts, BITS_PER_LONG);
	u32 waitbase = find_first_bit(&waitbases, BITS_PER_LONG);
	struct nvhost_driver *drv = to_nvhost_driver(ch->dev->dev.driver);

	if (drv->alloc_hwctx_handler) {
		ch->ctxhandler = drv->alloc_hwctx_handler(syncpt,
				waitbase, ch);
		if (!ch->ctxhandler)
			err = -ENOMEM;
	}

	return err;
}

static inline void __iomem *t114_channel_aperture(void __iomem *p, int ndx)
{
	p += NV_HOST1X_CHANNEL0_BASE;
	p += ndx * NV_HOST1X_CHANNEL_MAP_SIZE_BYTES;
	return p;
}

static int t114_channel_init(struct nvhost_channel *ch,
			    struct nvhost_master *dev, int index)
{
	ch->chid = index;
	mutex_init(&ch->reflock);
	mutex_init(&ch->submitlock);

	ch->aperture = t114_channel_aperture(dev->aperture, index);

	return t114_nvhost_hwctx_handler_init(ch);
}

int nvhost_init_t114_channel_support(struct nvhost_master *host,
	struct nvhost_chip_support *op)
{
	int result = nvhost_init_t20_channel_support(host, op);
	/* We're using 8 out of 9 channels supported by hw */
	op->channel.init = t114_channel_init;

	return result;
}

static void t114_free_nvhost_channel(struct nvhost_channel *ch)
{
	nvhost_free_channel_internal(ch, &t114_num_alloc_channels);
}

static struct nvhost_channel *t114_alloc_nvhost_channel(int chindex)
{
	return nvhost_alloc_channel_internal(chindex,
		NV_HOST1X_CHANNELS_T114, &t114_num_alloc_channels);
}

int nvhost_init_t114_support(struct nvhost_master *host,
	struct nvhost_chip_support *op)
{
	int err;

	/* don't worry about cleaning up on failure... "remove" does it. */
	err = nvhost_init_t114_channel_support(host, op);
	if (err)
		return err;
	err = host1x_init_cdma_support(op);
	if (err)
		return err;
	err = nvhost_init_t20_debug_support(op);
	if (err)
		return err;
	err = host1x_init_syncpt_support(host, op);
	if (err)
		return err;
	err = nvhost_init_t20_intr_support(op);
	if (err)
		return err;
	err = nvhost_memmgr_init(op);
	if (err)
		return err;
	op->nvhost_dev.alloc_nvhost_channel = t114_alloc_nvhost_channel;
	op->nvhost_dev.free_nvhost_channel = t114_free_nvhost_channel;

	/* Initialize T114 3D actmon */
	err = host1x_actmon_init(host);

	return 0;
}
