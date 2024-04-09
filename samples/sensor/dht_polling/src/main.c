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
		SENSOR_CHAN_AMBIENT_TEMP, SENSOR_CHAN_HUMIDITY);
static struct sensor_decoder_api *dht_decoder = SENSOR_DECODER_DT_GET(DT_ALIAS(dht0));
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
			rc = sensor_read(&dht_iodev, &dht_ctx, buf, 256);
			if (rc != 0) {
				printk("%s: sensor_read() failed: %d\n", dev->name, rc);
				return rc;
			}

			const struct sensor_decoder_api *decoder;
			struct sensor_q31_data decoded_data;

			rc = sensor_get_decoder(dev, &decoder);
			if (rc != 0) {
				printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
				return rc;
			}
			
			struct sensor_decode_context temp_ctx =
				SENSOR_DECODE_CONTEXT_INIT(decoder, buf, SENSOR_CHAN_AMBIENT_TEMP, 0);
			sensor_decode(&temp_ctx, &decoded_data, 1);

			// printk("%16s: temp is %s%d.%02d °C ", dev->name,
			// 	PRIq_arg(decoded_data.readings[0].temperature, 6, decoded_data.shift));

			uint32_t intg = __PRIq_arg_get_int(decoded_data.readings[0].temperature, 
				decoded_data.shift);
			uint32_t frac = __PRIq_arg_get_frac(decoded_data.readings[0].temperature, 2, 
				decoded_data.shift);

			printk("temp: %d.%02d\n", intg, frac);

			printk("%16s: temp is %s%d.%d °C ", dev->name,
				PRIq_arg(decoded_data.readings[0].temperature, 6, decoded_data.shift));

			struct sensor_decode_context hum_ctx = 
				SENSOR_DECODE_CONTEXT_INIT(decoder, buf, SENSOR_CHAN_HUMIDITY, 0);
			sensor_decode(&hum_ctx, &decoded_data, 1);

			// printk("humidity is %s%d.%02d %%RH\n",
			// 	PRIq_arg(decoded_data.readings[0].humidity, 6, decoded_data.shift));
		}
		k_msleep(1000);
	}
	return 0;
}
