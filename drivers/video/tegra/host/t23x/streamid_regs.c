/*
 * Copyright (c) 2016-2018, NVIDIA Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

struct nvhost_streamid_mapping {
	u32 host1x_offset;
	u32 client_offset;
	u32 client_limit;
};

static struct nvhost_streamid_mapping __attribute__((__unused__))
	t23x_host1x_streamid_mapping[] = {
	/* HOST1X_THOST_COMMON_SE1_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002000, 0x00000090, 0x00000090},
	/* HOST1X_THOST_COMMON_SE2_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002008, 0x00000090, 0x00000090},
	/* HOST1X_THOST_COMMON_SE4_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002010, 0x00000090, 0x00000090},
	/* HOST1X_THOST_COMMON_ISP_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002030, 0x00000800, 0x00000800},
	/* HOST1X_THOST_COMMON_VIC_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002038, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_NVENC_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002040, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_NVDEC_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002048, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_NVJPG_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002050, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_TSEC_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002058, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_TSECB_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002060, 0x00000030, 0x00000034},
	/* HOST1X_THOST_COMMON_VI_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002068, 0x00000800, 0x00000800},
	/* HOST1X_THOST_COMMON_VI_THI_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002070, 0x00000030, 0x00000034 },
	/* HOST1X_THOST_COMMON_ISP_THI_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002078, 0x00000030, 0x00000034 },
	/* HOST1X_THOST_COMMON_PVA0_CLUSTER_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002080, 0x00000000, 0x00000000 },
	/* HOST1X_THOST_COMMON_PVA1_CLUSTER_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002088, 0x00000000, 0x00000000 },
	/* HOST1X_THOST_COMMON_NVDLA0_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002090, 0x00000030, 0x00000034 },
	/* HOST1X_THOST_COMMON_NVDLA1_STRMID_0_OFFSET_BASE_0 */
	{ 0x00002098, 0x00000030, 0x00000034 },
	/* HOST1X_THOST_COMMON_NVENC1_STRMID_0_OFFSET_BASE_0 */
	{ 0x000020a0, 0x00000030, 0x00000034 },
	/* HOST1X_THOST_COMMON_NVDEC1_STRMID_0_OFFSET_BASE_0 */
	{ 0x000020a8, 0x00000030, 0x00000034 },
	{}
};
