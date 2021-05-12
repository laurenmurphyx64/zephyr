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

#define BUFLEN 300
int begin_index = 0;
const char *label = NULL;
const struct device *sensor = NULL;
int current_index = 0;

float bufx[BUFLEN] = { 0.0f };
float bufy[BUFLEN] = { 0.0f };
float bufz[BUFLEN] = { 0.0f };

bool initial = true;

TfLiteStatus SetupAccelerometer(tflite::ErrorReporter *error_reporter)
{
	#if defined(CONFIG_LSM6DSL)
	label = DT_LABEL(DT_INST(0, st_lsm6dsl));
	#elif defined(CONFIG_ADXL345)
	label = DT_LABEL(DT_INST(0, adi_adxl345));
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
	/* Skip if there is no data */
	if (!rc) {
		return false;
	}

	samples_count = rc;
	for (int i = 0; i < samples_count; i++) {
		rc = sensor_channel_get(sensor, SENSOR_CHAN_ACCEL_XYZ, accel);
		if (rc < 0) {
			TF_LITE_REPORT_ERROR(error_reporter, "ERROR: Update failed: %d\n", rc);
			return false;
		}
		bufx[begin_index] = (float)sensor_value_to_double(&accel[0]);
		bufy[begin_index] = (float)sensor_value_to_double(&accel[1]);
		bufz[begin_index] = (float)sensor_value_to_double(&accel[2]);
		
		// TODO: REMOVE
		// printf("AX=%10.6f AY=%10.6f AZ=%10.6f \n",
		//        bufx[begin_index],
		//        bufy[begin_index],
		//        bufz[begin_index]);
		
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
