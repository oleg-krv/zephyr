/*
 * Copyright (c) 2020 Alexander Kozhinov
 * AlexanderKozhinov@yandex.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <device.h>
#include <init.h>
#include <drivers/pinmux.h>
#include <sys/sys_io.h>

#include <pinmux/stm32/pinmux_stm32.h>

/* NUCLEO-F745ZI-Q pin configurations
 *
 * WARNING:
 * Possible pin conflicts:
 *          The pins PA2 and PB13 may conflict on selection of ETH_STM32_HAL,
 *          since they are used in ST Zio or ST morpho connectors.
 *          To avoid conflicting states the jumpers JP6 and JP7
 *          must be in ON state.
 */
static const struct pin_config pinconf[] = {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(mac), okay) && CONFIG_NET_L2_ETHERNET
	{ STM32_PIN_PA1, STM32H7_PINMUX_FUNC_PA1_ETH_REF_CLK },
	{ STM32_PIN_PA2, STM32H7_PINMUX_FUNC_PA2_ETH_MDIO },
	{ STM32_PIN_PA7, STM32H7_PINMUX_FUNC_PA7_ETH_CRS_DV },
	{ STM32_PIN_PC1, STM32H7_PINMUX_FUNC_PC1_ETH_MDC },
	{ STM32_PIN_PC4, STM32H7_PINMUX_FUNC_PC4_ETH_RXD0 },
	{ STM32_PIN_PC5, STM32H7_PINMUX_FUNC_PC5_ETH_RXD1 },
	{ STM32_PIN_PG11, STM32H7_PINMUX_FUNC_PG11_ETH_TX_EN },
	{ STM32_PIN_PG13, STM32H7_PINMUX_FUNC_PG13_ETH_TXD0 },
	{ STM32_PIN_PB13, STM32H7_PINMUX_FUNC_PB13_ETH_TXD1 },
#endif
};

static int pinmux_stm32_init(const struct device *port)
{
	ARG_UNUSED(port);

	stm32_setup_pins(pinconf, ARRAY_SIZE(pinconf));

	return 0;
}

SYS_INIT(pinmux_stm32_init, PRE_KERNEL_1,
	 CONFIG_PINMUX_STM32_DEVICE_INITIALIZATION_PRIORITY);
