/*
 * Copyright (c) 2020 Teslabs Engineering S.L.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9341_H_
#define ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9341_H_

#include <device.h>

/* Commands/registers. */
#define ILI9341_SWRESET 0x01 //
#define ILI9341_GAMSET 0x26 //
#define ILI9341_FRMCTR1 0xB1 //
#define ILI9341_DISCTRL 0xB6
#define ILI9341_ETMOD 0xB7 //
#define ILI9341_PWCTRL1 0xC0 //
#define ILI9341_PWCTRL2 0xC1 //
#define ILI9341_VMCTRL1 0xC5 //
#define ILI9341_VMCTRL2 0xC7 //
#define ILI9341_PWCTRLA 0xCB //
#define ILI9341_PWCTRLB 0xCF //
#define ILI9341_PGAMCTRL 0xE0 //
#define ILI9341_NGAMCTRL 0xE1 //
#define ILI9341_TIMCTRLA 0xE8 //
#define ILI9341_TIMCTRLB 0xEA //
#define ILI9341_PWSEQCTRL 0xED //
#define ILI9341_EN3G 0xF2 //
#define ILI9341_PUMPRATIOCTRL 0xF7 //


/* Commands/registers length. */
#define ILI9341_GAMSET_LEN 1U
#define ILI9341_FRMCTR1_LEN 2U
#define ILI9341_DISCTRL_LEN 3U
#define ILI9341_PWCTRL1_LEN 1U
#define ILI9341_PWCTRL2_LEN 1U
#define ILI9341_VMCTRL1_LEN 2U
#define ILI9341_VMCTRL2_LEN 1U
#define ILI9341_PGAMCTRL_LEN 15U
#define ILI9341_NGAMCTRL_LEN 15U

#define ILI9341_PWCTRLA_LEN 5U
#define ILI9341_PWCTRLB_LEN 3U
#define ILI9341_PWSEQCTRL_LEN 4U
#define ILI9341_TIMCTRLA_LEN 3U
#define ILI9341_TIMCTRLB_LEN 2U
#define ILI9341_PUMPRATIOCTRL_LEN 1U
#define ILI9341_EN3G_LEN 1U
#define ILI9341_ETMOD_LEN 1U


/** X resolution (pixels). */
#define ILI9341_X_RES 240U
/** Y resolution (pixels). */
#define ILI9341_Y_RES 320U

/** ILI9340 registers to be initialized. */
struct ili9341_regs {
	uint8_t gamset[ILI9341_GAMSET_LEN];
	uint8_t frmctr1[ILI9341_FRMCTR1_LEN];
	uint8_t disctrl[ILI9341_DISCTRL_LEN];
	uint8_t pwctrl1[ILI9341_PWCTRL1_LEN];
	uint8_t pwctrl2[ILI9341_PWCTRL2_LEN];
	uint8_t vmctrl1[ILI9341_VMCTRL1_LEN];
	uint8_t vmctrl2[ILI9341_VMCTRL2_LEN];
	uint8_t pgamctrl[ILI9341_PGAMCTRL_LEN];
	uint8_t ngamctrl[ILI9341_NGAMCTRL_LEN];

	uint8_t pwctrla[ILI9341_PWCTRLA_LEN];
	uint8_t pwctrlb[ILI9341_PWCTRLB_LEN];
	uint8_t pwseqctrl[ILI9341_PWSEQCTRL_LEN];
	uint8_t timctrla[ILI9341_TIMCTRLA_LEN];
	uint8_t timctrlb[ILI9341_TIMCTRLB_LEN];
	uint8_t pumpratio[ILI9341_PUMPRATIOCTRL_LEN];
	uint8_t en3g[ILI9341_EN3G_LEN];
	uint8_t entrymdset[ILI9341_ETMOD_LEN];
};

/* Initializer macro for ILI9341 registers. */
#define ILI9341_REGS_INIT(n)                                                   \
	static const struct ili9341_regs ili9xxx_regs_##n = {                  \
		.gamset = DT_PROP(DT_INST(n, ilitek_ili9341), gamset),         \
		.frmctr1 = DT_PROP(DT_INST(n, ilitek_ili9341), frmctr1),       \
		.disctrl = DT_PROP(DT_INST(n, ilitek_ili9341), disctrl),       \
		.pwctrl1 = DT_PROP(DT_INST(n, ilitek_ili9341), pwctrl1),       \
		.pwctrl2 = DT_PROP(DT_INST(n, ilitek_ili9341), pwctrl2),       \
		.vmctrl1 = DT_PROP(DT_INST(n, ilitek_ili9341), vmctrl1),       \
		.vmctrl2 = DT_PROP(DT_INST(n, ilitek_ili9341), vmctrl2),       \
		.pgamctrl = DT_PROP(DT_INST(n, ilitek_ili9341), pgamctrl),     \
		.ngamctrl = DT_PROP(DT_INST(n, ilitek_ili9341), ngamctrl),     \
		.pwctrla = DT_PROP(DT_INST(n, ilitek_ili9341), pwctrla),       \
		.pwctrlb = DT_PROP(DT_INST(n, ilitek_ili9341), pwctrlb),       \
		.pwseqctrl = DT_PROP(DT_INST(n, ilitek_ili9341), pwseqctrl),   \
		.timctrla = DT_PROP(DT_INST(n, ilitek_ili9341), timctrla),     \
		.timctrlb = DT_PROP(DT_INST(n, ilitek_ili9341), timctrlb),     \
		.pumpratio = DT_PROP(DT_INST(n, ilitek_ili9341), pumpratio),   \
		.en3g = DT_PROP(DT_INST(n, ilitek_ili9341), en3g),             \
		.entrymdset = DT_PROP(DT_INST(n, ilitek_ili9341), entrymdset), \
	}

/**
 * @brief Initialize ILI9341 registers with DT values.
 *
 * @param dev ILI9341 device instance
 * @return 0 on success, errno otherwise.
 */
int ili9341_regs_init(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_DISPLAY_DISPLAY_ILI9341_H_ */
