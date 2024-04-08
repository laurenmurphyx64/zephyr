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

#define DHT_ALIAS(i) DT_ALIAS(_CONCAT(dht, i))
#define DHT_DEVICE(i, _)                                                                 \
	IF_ENABLED(DT_NODE_EXISTS(DHT_ALIAS(i)), (DEVICE_DT_GET(DHT_ALIAS(i)),))

/* Support up to 10 temperature/humidity sensors */
static const struct device *const sensors[] = {LISTIFY(10, DHT_DEVICE, ())};

SENSOR_DT_READ_IODEV(dht_iodev, DT_ALIAS(dht0), 
		SENSOR_CHAN_AMBIENT_TEMP,
		SENSOR_CHAN_HUMIDITY);

RTIO_DEFINE_WITH_MEMPOOL(dht_ctx, 1, 1, 1, 64, 4);

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

			struct sensor_q31_data q31 = {0};
			uint8_t buf[128];
			rc = sensor_read(&dht_iodev, &dht_ctx, buf, 128);
			if (rc != 0) {
				printk("%s: sensor_read() failed: %d\n", dev->name, rc);
				return rc;
			}
			printf("buf: %f\n", buf[0]);

			// const struct sensor_decoder_api *decoder;
			// struct sensor_q31_data decoded_data;

			// rc = sensor_get_decoder(dev, &decoder);
			// if (rc != 0) {
			// 	printk("%s: sensor_get_decode() failed: %d\n", dev->name, rc);
			// 	return rc;
			// }
			// struct sensor_decode_context ctx =
			// 	SENSOR_DECODE_CONTEXT_INIT(decoder, NULL, SENSOR_CHAN_AMBIENT_TEMP, 0);
			// sensor_decode(&ctx, &decoded_data, 1);

			// q31_t temp_q31_t = decoded_data.readings[0].temperature;
			// int sign = (temp_q31_t & 0x80000000) ? -1 : 1;
			// uint32_t fraction = temp_q31_t & 0x7FFFFFFF;
			// double temp = sign * (double) fraction / (double)(1ULL << 31);

			// printk("%16s: temp is %f °C humidity is %f %%RH\n",
			// 				dev->name, temp, 0.0);
		}
		k_msleep(1000);
	}
	return 0;
}
