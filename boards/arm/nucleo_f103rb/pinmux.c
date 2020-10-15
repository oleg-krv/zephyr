/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <device.h>
#include <init.h>
#include <drivers/pinmux.h>
#include <sys/sys_io.h>

#include <pinmux/stm32/pinmux_stm32.h>

/* pin assignments for NUCLEO-F103RB board */
static const struct pin_config pinconf[] = {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi1), okay) && CONFIG_SPI
#ifdef CONFIG_SPI_STM32_USE_HW_SS
	{STM32_PIN_PA4, STM32F1_PINMUX_FUNC_PA4_SPI1_MASTER_NSS},
#endif /* CONFIG_SPI_STM32_USE_HW_SS */
	{STM32_PIN_PA5, STM32F1_PINMUX_FUNC_PA5_SPI1_MASTER_SCK},
	{STM32_PIN_PA6, STM32F1_PINMUX_FUNC_PA6_SPI1_MASTER_MISO},
	{STM32_PIN_PA7, STM32F1_PINMUX_FUNC_PA7_SPI1_MASTER_MOSI},
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi2), okay) && CONFIG_SPI
#ifdef CONFIG_SPI_STM32_USE_HW_SS
	{STM32_PIN_PB12, STM32F1_PINMUX_FUNC_PB12_SPI2_MASTER_NSS},
#endif /* CONFIG_SPI_STM32_USE_HW_SS */
	{STM32_PIN_PB13, STM32F1_PINMUX_FUNC_PB13_SPI2_MASTER_SCK},
	{STM32_PIN_PB14, STM32F1_PINMUX_FUNC_PB14_SPI2_MASTER_MISO},
	{STM32_PIN_PB15, STM32F1_PINMUX_FUNC_PB15_SPI2_MASTER_MOSI},
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
