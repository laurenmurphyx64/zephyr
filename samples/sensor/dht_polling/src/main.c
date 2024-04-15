/*
 * Copyright (c) 2023 Ian Morris
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/dsp/print_format.h>

#define DHT_ALIAS(i) DT_ALIAS(_CONCAT(dht, i))
#define DHT_DEVICE(i, _)                                                                 \
	IF_ENABLED(DT_NODE_EXISTS(DHT_ALIAS(i)), (DEVICE_DT_GET(DHT_ALIAS(i)),))

/* Support up to 10 temperature/humidity sensors */
static const struct device *const sensors[] = {LISTIFY(10, DHT_DEVICE, ())};

SENSOR_DT_READ_IODEV(dht_iodev, DT_ALIAS(dht0),
		{SENSOR_CHAN_AMBIENT_TEMP, 0},
		{SENSOR_CHAN_HUMIDITY, 0});
RTIO_DEFINE(dht_ctx, 1, 1);

int main(void)
{
	int rc;

	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		if (!device_is_ready(sensors[i])) {
			printk("sensor: device %s not ready.\n", sensors[i]->name);
			return 0;
		}
	}

	while (1) {
		for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
			struct device *dev = (struct device *)sensors[i];

			uint8_t buf[128];
			rc = sensor_read(&dht_iodev, &dht_ctx, buf, 128);
			if (rc != 0) {
				printk("%s: sensor_read() failed: %d\n", dev->name, rc);
				return rc;
			}

			// freezes
			// what's the difference? 
			// struct sensor_decode_context temp_ctx =
			// 	SENSOR_DECODE_CONTEXT_INIT(decoder, buf, SENSOR_CHAN_AMBIENT_TEMP, 0);
			// sensor_decode(&temp_ctx, &decoded_data, 1);

			const struct sensor_decoder_api *decoder;

			rc = sensor_get_decoder(dev, &decoder);
			if (rc != 0) {
				printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
				return rc;
			}

			uint8_t decoded_buf[128];
			uint32_t fit = 0;
			decoder->decode(buf, (struct sensor_chan_spec) {SENSOR_CHAN_AMBIENT_TEMP, 0},
					&fit, 1, decoded_buf);
			struct sensor_q31_data *data =
					(struct sensor_q31_data *)decoded_buf;

			printk("NEW API: %16s: temp is %s%d.%d °C q temp is %d\n", dev->name,
				PRIq_arg(data->readings[0].temperature, 6, data->shift),
				data->readings[0].temperature);

			/* original sample */
			rc = sensor_sample_fetch(dev);
			if (rc < 0) {
				printk("%s: sensor_sample_fetch() failed: %d\n", dev->name, rc);
				return rc;
			}

			struct sensor_value temp;
			struct sensor_value hum;

			rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
			if (rc == 0) {
				rc = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum);
			}
			if (rc != 0) {
				printf("get failed: %d\n", rc);
				break;
			}

			printk("OLD API: %16s: temp is %d.%02d °C humidity is %d.%02d %%RH\n",
							dev->name, temp.val1, temp.val2 / 10000,
							hum.val1, hum.val2 / 10000);
		}
		k_msleep(1000);
	}
	return 0;
}
