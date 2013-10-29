/*
 * drivers/misc/tegra-cec/tegra_cec.h
 *
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/pm.h>
#include <asm/atomic.h>


struct tegra_cec {
	struct device		*dev;
	struct miscdevice 	misc_dev;
	struct clk		*clk;
	void __iomem		*cec_base;
	int			tegra_cec_irq;
	wait_queue_head_t	rx_waitq;
	wait_queue_head_t	tx_waitq;
	wait_queue_head_t	init_waitq;
	unsigned int		rx_wake;
	unsigned int		tx_wake;
	unsigned short		rx_buffer;
	atomic_t		init_done;
	struct work_struct	work;
};
static int tegra_cec_remove(struct platform_device *pdev);

/*CEC Timing registers*/
#define TEGRA_CEC_SW_CONTROL 	 0X000
#define TEGRA_CEC_HW_CONTROL	 0X004
#define TEGRA_CEC_INPUT_FILTER	 0X008
#define TEGRA_CEC_TX_REGISTER	 0X010
#define TEGRA_CEC_RX_REGISTER	 0X014
#define TEGRA_CEC_RX_TIMING_0	 0X018
#define TEGRA_CEC_RX_TIMING_1	 0X01C
#define TEGRA_CEC_RX_TIMING_2	 0X020
#define TEGRA_CEC_TX_TIMING_0	 0X024
#define TEGRA_CEC_TX_TIMING_1	 0X028
#define TEGRA_CEC_TX_TIMING_2	 0X02C
#define TEGRA_CEC_INT_STAT	 0X030
#define TEGRA_CEC_INT_MASK	 0X034
#define TEGRA_CEC_HW_DEBUG_RX	 0X038
#define TEGRA_CEC_HW_DEBUG_TX	 0X03C

#define TEGRA_CEC_LOGICAL_ADDR	0x10

#define TEGRA_CEC_HW_CONTROL_RX_LOGICAL_ADDRS_MASK	0
#define TEGRA_CEC_HW_CONTROL_RX_SNOOP 			(1<<15)
#define TEGRA_CEC_HW_CONTROL_RX_NAK_MODE 		(1<<16)
#define TEGRA_CEC_HW_CONTROL_TX_NAK_MODE		(1<<24)
#define TEGRA_CEC_HW_CONTROL_FAST_SIM_MODE 		(1<<30)
#define TEGRA_CEC_HW_CONTROL_TX_RX_MODE			(1<<31)

#define TEGRA_CEC_INPUT_FILTER_MODE		(1<<31)
#define TEGRA_CEC_INPUT_FILTER_FIFO_LENGTH_MASK	0

#define TEGRA_CEC_TX_REGISTER_DATA_MASK	 		0
#define TEGRA_CEC_TX_REGISTER_EOM_MASK	 		8
#define TEGRA_CEC_TX_REGISTER_ADDRESS_MODE_MASK	 	12
#define TEGRA_CEC_TX_REGISTER_GENERATE_START_BIT_MASK	16
#define TEGRA_CEC_TX_REGISTER_RETRY_FRAME_MASK	 	17

#define TEGRA_CEC_RX_REGISTER_MASK	 0
#define TEGRA_CEC_RX_REGISTER_EOM	 (1<<8)
#define TEGRA_CEC_RX_REGISTER_ACK	 (1<<9)

#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MAX_LO_TIME_MASK	 0
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MIN_LO_TIME_MASK	 8
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MAX_DURATION_MASK	 16
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MIN_DURATION_MASK	 24

#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MAX_LO_TIME_MASK	 0
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_SAMPLE_TIME_MASK	 8
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MAX_DURATION_MASK	 16
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MIN_DURATION_MASK	 24

#define TEGRA_CEC_RX_TIMING_2_RX_END_OF_BLOCK_TIME_MASK	 0

#define TEGRA_CEC_TX_TIMING_0_TX_START_BIT_LO_TIME_MASK	 	0
#define TEGRA_CEC_TX_TIMING_0_TX_START_BIT_DURATION_MASK	8
#define TEGRA_CEC_TX_TIMING_0_TX_BUS_XITION_TIME_MASK	 	16
#define TEGRA_CEC_TX_TIMING_0_TX_BUS_ERROR_LO_TIME_MASK	 	24

#define TEGRA_CEC_TX_TIMING_1_TX_LO_DATA_BIT_LO_TIME_MASK	0
#define TEGRA_CEC_TX_TIMING_1_TX_HI_DATA_BIT_LO_TIME_MASK	8
#define TEGRA_CEC_TX_TIMING_1_TX_DATA_BIT_DURATION_MASK	 	16
#define TEGRA_CEC_TX_TIMING_1_TX_ACK_NAK_BIT_SAMPLE_TIME_MASK	24

#define TEGRA_CEC_TX_TIMING_2_BUS_IDLE_TIME_ADDITIONAL_FRAME_MASK	0
#define TEGRA_CEC_TX_TIMING_2_TX_BUS_IDLE_TIME_NEW_FRAME_MASK		4
#define TEGRA_CEC_TX_TIMING_2_TX_BUS_IDLE_TIME_RETRY_FRAME_MASK	 	8

#define TEGRA_CEC_INT_STAT_TX_REGISTER_EMPTY	 		(1<<0)
#define TEGRA_CEC_INT_STAT_TX_REGISTER_UNDERRUN	 		(1<<1)
#define TEGRA_CEC_INT_STAT_TX_FRAME_OR_BLOCK_NAKD		(1<<2)
#define TEGRA_CEC_INT_STAT_TX_ARBITRATION_FAILED		(1<<3)
#define TEGRA_CEC_INT_STAT_TX_BUS_ANOMALY_DETECTED		(1<<4)
#define TEGRA_CEC_INT_STAT_TX_FRAME_TRANSMITTED			(1<<5)
#define TEGRA_CEC_INT_STAT_RX_REGISTER_FULL			(1<<8)
#define TEGRA_CEC_INT_STAT_RX_REGISTER_OVERRUN			(1<<9)
#define TEGRA_CEC_INT_STAT_RX_START_BIT_DETECTED		(1<<10)
#define TEGRA_CEC_INT_STAT_RX_BUS_ANOMALY_DETECTED		(1<<11)
#define TEGRA_CEC_INT_STAT_RX_BUS_ERROR_DETECTED		(1<<12)
#define TEGRA_CEC_INT_STAT_FILTERED_RX_DATA_PIN_TRANSITION_H2L	(1<<13)
#define TEGRA_CEC_INT_STAT_FILTERED_RX_DATA_PIN_TRANSITION_L2H	(1<<14)

#define TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY	 		(1<<0)
#define TEGRA_CEC_INT_MASK_TX_REGISTER_UNDERRUN	 		(1<<1)
#define TEGRA_CEC_INT_MASK_TX_FRAME_OR_BLOCK_NAKD		(1<<2)
#define TEGRA_CEC_INT_MASK_TX_ARBITRATION_FAILED		(1<<3)
#define TEGRA_CEC_INT_MASK_TX_BUS_ANOMALY_DETECTED		(1<<4)
#define TEGRA_CEC_INT_MASK_TX_FRAME_TRANSMITTED			(1<<5)
#define TEGRA_CEC_INT_MASK_RX_REGISTER_FULL			(1<<8)
#define TEGRA_CEC_INT_MASK_RX_REGISTER_OVERRUN			(1<<9)
#define TEGRA_CEC_INT_MASK_RX_START_BIT_DETECTED		(1<<10)
#define TEGRA_CEC_INT_MASK_RX_BUS_ANOMALY_DETECTED		(1<<11)
#define TEGRA_CEC_INT_MASK_RX_BUS_ERROR_DETECTED		(1<<12)
#define TEGRA_CEC_INT_MASK_FILTERED_RX_DATA_PIN_TRANSITION_H2L	(1<<13)
#define TEGRA_CEC_INT_MASK_FILTERED_RX_DATA_PIN_TRANSITION_L2H	(1<<14)

#define TEGRA_CEC_HW_DEBUG_TX_DURATION_COUNT_MASK	0
#define TEGRA_CEC_HW_DEBUG_TX_TXBIT_COUNT_MASK		17
#define TEGRA_CEC_HW_DEBUG_TX_STATE_MASK		21
#define TEGRA_CEC_HW_DEBUG_TX_FORCELOOUT		(1<<25)
#define TEGRA_CEC_HW_DEBUG_TX_TXDATABIT_SAMPLE_TIMER	(1<<26)

#define TEGRA_CEC_NAME "tegra_cec"
