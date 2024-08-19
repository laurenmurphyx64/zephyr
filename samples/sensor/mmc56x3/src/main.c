/*
 * Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>

const struct device *const dev = DEVICE_DT_GET_ANY(memsic_mmc56x3);

#ifdef CONFIG_SENSOR_ASYNC_API
SENSOR_DT_READ_IODEV(iodev, DT_COMPAT_GET_ANY_STATUS_OKAY(memsic_mmc56x3),
		     {SENSOR_CHAN_AMBIENT_TEMP, 0}, {SENSOR_CHAN_MAGN_XYZ, 0});
RTIO_DEFINE(ctx, 1, 1);
#endif

static const struct device *check_mmc56x3_device(void)
{
	if (dev == NULL) {
		printk("Error: Device not found.\n");
		return NULL;
	}
	if (!device_is_ready(dev)) {
		printk("Error: Device %s is not ready.\n", dev->name);
		return NULL;
	}

	printk("Found device \"%s\", getting sensor data\n", dev->name);
	return dev;
}

int main(void)
{
	const struct device *dev = check_mmc56x3_device();

	if (dev == NULL) {
		return 0;
	}

	int rc;

	while (1) {
#ifdef CONFIG_SENSOR_ASYNC_API
		uint8_t buf[64];

		rc = sensor_read(&iodev, &ctx, buf, 64);

		if (rc != 0) {
			printk("%s: sensor_read() error: %d\n", dev->name, rc);
			return rc;
		}

		const struct sensor_decoder_api *decoder;

		rc = sensor_get_decoder(dev, &decoder);

		if (rc != 0) {
			printk("%s: sensor_get_decoder() error: %d\n", dev->name, rc);
			return rc;
		}

		uint32_t temp_fit = 0;
		struct sensor_q31_data temp_data = {0};

		decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0},
				&temp_fit, 1, &temp_data);

		printk("%s: %s%d.%d C\n", dev->name,
		       PRIq_arg(temp_data.readings[0].temperature, 6, temp_data.shift));

		uint32_t magn_fit = 0;
		struct sensor_three_axis_data magn_data = {0};

		decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0}, &magn_fit,
				1, &magn_data);

		printk("%s: MX=%s%d.%d MY=%s%d.%d MZ=%s%d.%d\n", dev->name,
		       PRIq_arg(magn_data.readings[0].x, 6, magn_data.shift),
		       PRIq_arg(magn_data.readings[0].y, 6, magn_data.shift),
		       PRIq_arg(magn_data.readings[0].z, 6, magn_data.shift));
#else
		struct sensor_value temp;

		rc = sensor_sample_fetch(dev);
		if (rc != 0) {
			printk("sensor_sample_fetch() error: %d\n", rc);
			break;
		}

		rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		if (rc != 0) {
			printk("sensor_channel_get() error: %d\n", rc);
			break;
		}

		printk("%s: %g C\n", dev->name, sensor_value_to_double(&temp));

		struct sensor_value magn[3];

		rc = sensor_channel_get(dev, SENSOR_CHAN_MAGN_XYZ, magn);
		if (rc != 0) {
			printk("sensor_channel_get error: %d\n", rc);
			break;
		}

		printk("%s: MX=%g MY=%g MZ=%g\n", dev->name, sensor_value_to_double(&magn[0]),
		       sensor_value_to_double(&magn[1]), sensor_value_to_double(&magn[2]));
#endif
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
