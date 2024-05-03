/*
 * Copyright (c) 2024 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bme280.h"
#include <math.h>

static int bme280_decoder_get_frame_count(const uint8_t *buffer,
					     struct sensor_chan_spec chan_spec,
					     uint16_t *frame_count)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(chan_spec);

	/* This sensor lacks a FIFO; there will always only be one frame at a time. */
	*frame_count = 1;
	return 0;
}

static int bme280_decoder_get_size_info(struct sensor_chan_spec chan_spec, size_t *base_size,
					   size_t *frame_size)
{
	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
	case SENSOR_CHAN_HUMIDITY:
	case SENSOR_CHAN_PRESS:
		*base_size = sizeof(struct sensor_q31_sample_data);
		*frame_size = sizeof(struct sensor_q31_sample_data);
		return 0;
	default:
		return -ENOTSUP;
	}
}

#define BME280_HUM_SHIFT (22)
#define BME280_PRESS_SHIFT (24)
#define BME280_TEMP_SHIFT (24)

static void bme280_convert_signed_temp_raw_to_q31(int32_t reading, q31_t *out)
{
	double temp_double = reading / 100.0;

	temp_double = temp_double * pow(2, 31 - BME280_TEMP_SHIFT);

	int64_t temp_round = (temp_double < 0) ? (temp_double - 0.5) : (temp_double + 0.5);
	int32_t temp = CLAMP(temp_round, INT32_MIN, INT32_MAX);

	if (temp < 0) {
		temp = abs(temp);
		temp = ~temp;
		temp++;
	}

	*out = temp;
}

static void bme280_convert_uq32_to_q31(uint32_t reading, q31_t *out)
{
	*out = 0xEFFF & reading;
}

static int bme280_decoder_decode(const uint8_t *buffer, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	const struct bme280_encoded_data *edata = (const struct bme280_encoded_data *)buffer;

	if (*fit != 0) {
		return 0;
	}

	struct sensor_q31_data *out = data_out;

	out->header.base_timestamp_ns = edata->header.timestamp;
	out->header.reading_count = 1;

	switch (chan_spec.chan_type) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		bme280_convert_signed_temp_raw_to_q31(edata->data.comp_temp,
			&out->readings[0].temperature);
		out->shift = BME280_TEMP_SHIFT;
		break;
	case SENSOR_CHAN_PRESS:
		bme280_convert_uq32_to_q31(edata->data.comp_press,
			&out->readings[0].pressure);
		out->shift = BME280_PRESS_SHIFT;
		break;
	case SENSOR_CHAN_HUMIDITY:
		bme280_convert_uq32_to_q31(edata->data.comp_humidity,
			&out->readings[0].humidity);
		out->shift = BME280_HUM_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	*fit = 1;

	return 1;
}


SENSOR_DECODER_API_DT_DEFINE() = {
	.get_frame_count = bme280_decoder_get_frame_count,
	.get_size_info = bme280_decoder_get_size_info,
	.decode = bme280_decoder_decode,
};

int bme280_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);
	*decoder = &SENSOR_DECODER_NAME();

	return 0;
}
