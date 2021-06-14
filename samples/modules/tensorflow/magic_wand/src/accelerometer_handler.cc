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

struct sensor_value attr_fxos8700 = {
		.val1 = 400,
		.val2 = 0,
};

bool initial = true;

static inline float convert_ms2_to_mg(struct sensor_value *val)
{
	return (((val->val1 + (float)val->val2 / 1000000) / MS2_IN_G) * 1000);
}

TfLiteStatus SetupAccelerometer(tflite::ErrorReporter *error_reporter)
{
	/* Give Zephyr boot a good 3s to finish setting up the acclerometer
	 * (TODO: Remove)
	 */
	k_sleep(K_MSEC(3000));

	/* Get label */
#if defined(CONFIG_ADXL345)
	label = DT_LABEL(DT_INST(0, adi_adxl345));
#elif defined(CONFIG_FXOS8700)
	label = DT_LABEL(DT_INST(0, nxp_fxos8700));
#else
	TF_LITE_REPORT_ERROR(error_reporter,
					"Unsupported accelerometer\n");
	return kTfLiteError;
#endif

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

	/* Set sampling frequency for FXOS8700 
	 * (TODO: Re-enable once driver (?) is fixed)
	 */
#ifdef CONFIG_FXOS8700
	// if (sensor_attr_set(sensor, SENSOR_CHAN_ALL,
	// 		    SENSOR_ATTR_SAMPLING_FREQUENCY, &attr_fxos8700)) {
	// 	TF_LITE_REPORT_ERROR(error_reporter, "Could not set sampling frequency.\n",
	// 			     label);
	// 	return kTfLiteError;
	// }
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
#if defined(CONFIG_ADXL345)
	/* Skip if there is no data */
	if (!rc) {
		return false;
	}
	/* ADXL345 uses return value of sensor_sample_fetch as sample count */
	samples_count = rc; 
#else
	samples_count = 10; /* arbitrary; 66 would collect for 1 second */
#endif

	printk("***\n");
	for (int i = 0; i < samples_count; i++) {
#if defined(CONFIG_FXOS8700)
		k_sleep(K_MSEC(15));
		sensor_sample_fetch(sensor);
#endif
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
		bufx[begin_index] = (float)convert_ms2_to_mg(&accel[0]);
		bufy[begin_index] = (float)convert_ms2_to_mg(&accel[1]);
		bufz[begin_index] = (float)convert_ms2_to_mg(&accel[2]);

		/* TODO: Remove */
		// /*
		printf("%04.2f,%04.2f,%04.2f\r\n", 
			bufx[begin_index],
			bufy[begin_index],
			bufz[begin_index]);
		// */
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
