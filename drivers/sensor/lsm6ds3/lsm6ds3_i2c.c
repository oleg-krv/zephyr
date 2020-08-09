/* ST Microelectronics LSM6DS3 6-axis IMU sensor driver
 *
 * Copyright (c) 2019 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Datasheet:
 * https://www.st.com/resource/en/datasheet/lsm6ds3.pdf
 */

#define DT_DRV_COMPAT st_lsm6ds3

#include <string.h>
#include <drivers/i2c.h>
#include <logging/log.h>

#include "lsm6ds3.h"

#if DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)

LOG_MODULE_DECLARE(LSM6DS3, CONFIG_SENSOR_LOG_LEVEL);

static int lsm6ds3_i2c_read(struct device *dev, uint8_t reg_addr,
			    uint8_t *value, uint8_t len)
{
	struct lsm6ds3_data *data = dev->driver_data;
	const struct lsm6ds3_config *cfg = dev->config_info;

	return i2c_burst_read(data->bus, cfg->i2c_slv_addr,
			      reg_addr, value, len);
}

static int lsm6ds3_i2c_write(struct device *dev, uint8_t reg_addr,
			     uint8_t *value, uint8_t len)
{
	struct lsm6ds3_data *data = dev->driver_data;
	const struct lsm6ds3_config *cfg = dev->config_info;

	return i2c_burst_write(data->bus, cfg->i2c_slv_addr,
			       reg_addr, value, len);
}

int lsm6ds3_i2c_init(struct device *dev)
{
	struct lsm6ds3_data *data = dev->driver_data;

	data->ctx_i2c.read_reg = (stmdev_read_ptr) lsm6ds3_i2c_read,
	data->ctx_i2c.write_reg = (stmdev_write_ptr) lsm6ds3_i2c_write,

	data->ctx = &data->ctx_i2c;
	data->ctx->handle = dev;

	return 0;
}
#endif /* DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c) */
