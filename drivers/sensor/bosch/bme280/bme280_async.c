/*
 * Copyright (c) 2024 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include "bme280.h"

LOG_MODULE_DECLARE(BME280, CONFIG_SENSOR_LOG_LEVEL);

int bme280_submit(const struct device *dev, struct rtio_iodev_sqe *iodev_sqe)
{
	uint32_t min_buf_len = sizeof(struct bme280_encoded_data);
	int rc;
	uint8_t *buf;
	uint32_t buf_len;
	struct bme280_encoded_data *edata;

	rc = rtio_sqe_rx_buf(iodev_sqe, min_buf_len, min_buf_len, &buf, &buf_len);
	if (rc != 0) {
		LOG_ERR("Failed to get a read buffer of size %u bytes", min_buf_len);
		rtio_iodev_sqe_err(iodev_sqe, rc);
		return rc;
	}

	edata = (struct bme280_encoded_data *)buf;
	edata->header.timestamp = k_ticks_to_ns_floor64(k_uptime_ticks());

	rc = bme280_sample_fetch_helper(dev, SENSOR_CHAN_ALL, &edata->data);
	if (rc != 0) {
		LOG_ERR("Failed to fetch samples");
		rtio_iodev_sqe_err(iodev_sqe, rc);
		return rc;
	}

	rtio_iodev_sqe_ok(iodev_sqe, 0);

	return 0;
}
