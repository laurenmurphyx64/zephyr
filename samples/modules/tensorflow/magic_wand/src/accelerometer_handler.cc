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

static inline float convert_ms2_to_mg(struct sensor_value *val)
{
	return (((val->val1 + (float)val->val2 / 1000000) / MS2_IN_G) * 1000);
}

TfLiteStatus SetupAccelerometer(tflite::ErrorReporter *error_reporter)
{
	/* Get label */
	label = DT_LABEL(DT_INST(0, nxp_fxos8700));

	/* Get binding */
	sensor = device_get_binding(label);
	if (sensor == NULL) {
		TF_LITE_REPORT_ERROR(error_reporter,
				     "Failed to get accelerometer, label: %s\n",
				     label);
		return kTfLiteError;
	} else {
		TF_LITE_REPORT_ERROR(error_reporter, "Got accelerometer, label: %s\n",
				     label);
	}

	struct sensor_value attr = {
		.val1 = 6,
		.val2 = 250000,
	};

	if (sensor_attr_set(sensor, SENSOR_CHAN_ALL,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &attr)) {
		TF_LITE_REPORT_ERROR(error_reporter, "Could not set sampling frequency.\n",
				     label);
		return kTfLiteError;
	}

	return kTfLiteOk;
}

bool ReadAccelerometer(tflite::ErrorReporter *error_reporter, float *input,
		       int length)
{
	int rc;
	struct sensor_value accel[3];
	
	rc = sensor_sample_fetch(sensor);
	if (rc < 0) {
		TF_LITE_REPORT_ERROR(error_reporter, "Fetch failed\n");
		return false;
	}

	rc = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (rc < 0) {
		TF_LITE_REPORT_ERROR(error_reporter, "Update failed: %d\n", rc);
		return false;
	}

	printk("%04.2f,%04.2f,%04.2f\r\n", 
		(float)convert_ms2_to_mg(&accel_x_out),
		(float)convert_ms2_to_mg(&accel_y_out),
		(float)convert_ms2_to_mg(&accel_z_out));

	return true;
}
