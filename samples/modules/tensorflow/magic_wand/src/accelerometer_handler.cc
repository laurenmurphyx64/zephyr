/*
 * Copyright 2019 The TensorFlow Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "accelerometer_handler.h"

#include <device.h>
#include <drivers/sensor.h>
#include <stdio.h>
#include <string.h>
#include <zephyr.h>

#define MS2_IN_G 9.80665
#define BUFLEN 300
int begin_index = 0;
const char *label = NULL;
const struct device *sensor = NULL;
int current_index = 0;

float bufx[BUFLEN] = { 0.0f };
float bufy[BUFLEN] = { 0.0f };
float bufz[BUFLEN] = { 0.0f };

bool initial = true;

static struct sensor_value accel_x_out, accel_y_out, accel_z_out;
/* Set sampling rate to 140 Hz */
const struct sensor_value odr_attr = {
	.val1 = 104,
	.val2 = 0
};

#ifdef CONFIG_LSM6DSL_TRIGGER
static void lsm6dsl_trigger_handler(const struct device *dev,
				    struct sensor_trigger *trig)
{
	static struct sensor_value accel_x, accel_y, accel_z;

	sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel_x);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel_y);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel_z);

	accel_x_out = accel_x;
	accel_y_out = accel_y;
	accel_z_out = accel_z;
}
#endif

static inline float convert_ms2_to_mg(struct sensor_value *val)
{
	return (((val->val1 + (float)val->val2 / 1000000) / MS2_IN_G) * 1000);
}

TfLiteStatus SetupAccelerometer(tflite::ErrorReporter *error_reporter)
{
	k_sleep(K_MSEC(1000)); /* Allow the board time to set up accelerometer */

#if defined(CONFIG_ADXL345)
	label = DT_LABEL(DT_INST(0, adi_adxl345));
#elif defined(CONFIG_LSM6DSL)
	label = DT_LABEL(DT_INST(0, st_lsm6dsl));
#else
	TF_LITE_REPORT_ERROR(error_reporter,
					"Unsupported accelerometer\n");
#endif

	sensor = device_get_binding(label);
	if (sensor == NULL) {
		TF_LITE_REPORT_ERROR(error_reporter,
				     "Failed to get accelerometer, label: %s\n",
				     label);
	} else {
		TF_LITE_REPORT_ERROR(error_reporter, "Got accelerometer, label: %s\n",
				     label);
	}

#if defined(CONFIG_LSM6DSL)
	/* set accel sampling frequency to 104 Hz */
	if (sensor_attr_set(sensor, SENSOR_CHAN_ACCEL_XYZ,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) < 0) {
		TF_LITE_REPORT_ERROR(error_reporter,
					"Cannot set sampling frequency for accelerometer.\n");
	}
#ifdef CONFIG_LSM6DSL_TRIGGER
	struct sensor_trigger trig;

	trig.type = SENSOR_TRIG_DATA_READY;
	trig.chan = SENSOR_CHAN_ACCEL_XYZ;

	if (sensor_trigger_set(sensor, &trig, lsm6dsl_trigger_handler) != 0) {
		TF_LITE_REPORT_ERROR(error_reporter,
					"Could not set sensor type and channel\n");
	}
#endif
	if (sensor_sample_fetch(sensor) < 0) {
		TF_LITE_REPORT_ERROR(error_reporter,
					"Sensor sample update error\n");
	}
#endif

	return kTfLiteOk;
}

bool ReadAccelerometer(tflite::ErrorReporter *error_reporter, float *input,
		       int length)
{
	int rc;
	struct sensor_value accel[3];
	int samples_count;

	rc = sensor_sample_fetch(sensor);
	if (rc < 0) {
		TF_LITE_REPORT_ERROR(error_reporter, "Fetch failed\n");
		return false;
	}
	/* ADXL345 uses return value of sensor_sample_fetch as sample count */
#if defined(CONFIG_ADXL345)
	/* Skip if there is no data */
	if (!rc) {
		return false;
	}
	samples_count = rc;
#else
	samples_count = 1;
#endif

	for (int i = 0; i < samples_count; i++) {
		rc = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, accel);
		if (rc < 0) {
			TF_LITE_REPORT_ERROR(error_reporter, "ERROR: Update failed: %d\n", rc);
			return false;
		}

#if defined(CONFIG_ADXL345)
		bufx[begin_index] = (float)sensor_value_to_double(&accel[0]);
		bufy[begin_index] = (float)sensor_value_to_double(&accel[1]);
		bufz[begin_index] = (float)sensor_value_to_double(&accel[2]);
#else
		bufx[begin_index] = (float)convert_ms2_to_mg(&accel_x_out);
		bufy[begin_index] = (float)convert_ms2_to_mg(&accel_y_out);
		bufz[begin_index] = (float)convert_ms2_to_mg(&accel_z_out);
#endif
		begin_index++;
		if (begin_index >= BUFLEN) {
			begin_index = 0;
		}
	}

	if (initial && begin_index >= 100) {
		initial = false;
	}

	if (initial) {
		return false;
	}

	int sample = 0;
	for (int i = 0; i < (length - 3); i += 3) {
		int ring_index = begin_index + sample - length / 3;
		if (ring_index < 0) {
			ring_index += BUFLEN;
		}
		input[i] = bufx[ring_index];
		input[i + 1] = bufy[ring_index];
		input[i + 2] = bufz[ring_index];
		sample++;
	}
	return true;
}
