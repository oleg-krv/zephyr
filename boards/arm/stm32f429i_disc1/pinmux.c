/*
 * Copyright (c) 2016 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <device.h>
#include <init.h>
#include <drivers/pinmux.h>
#include <sys/sys_io.h>

#include <pinmux/stm32/pinmux_stm32.h>

/* pin assignments for STM32F429I-DISC1 board */
static const struct pin_config pinconf[] = {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi5), okay) && CONFIG_SPI
#ifdef CONFIG_SPI_STM32_USE_HW_SS
	{STM32_PIN_PF6, STM32F4_PINMUX_FUNC_PF6_SPI5_MASTER_NSS},
#endif /* CONFIG_SPI_STM32_USE_HW_SS */
	{STM32_PIN_PF7, STM32F4_PINMUX_FUNC_PF7_SPI5_MASTER_SCK},
	{STM32_PIN_PF8, STM32F4_PINMUX_FUNC_PF8_SPI5_MASTER_MISO},
	{STM32_PIN_PF9, STM32F4_PINMUX_FUNC_PF9_SPI5_MASTER_MOSI},
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
