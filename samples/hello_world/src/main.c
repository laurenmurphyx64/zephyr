/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/types.h>
#include <zephyr/dsp/print_format.h>
#include <zephyr/sys/util.h>
#include "fixedptc.h"

#define SHIFT 16
#define RES 100

int main(void)
{
	printf("Hello World! %s\n", CONFIG_BOARD);

	int32_t read = 2308;
	// int32_t read = -2345;

	// int32_t readq = fixedpt_fromint(read);
	// int32_t resq =  fixedpt_fromint(RES);
	// int32_t resultq = fixedpt_div(readq, resq);

	int64_t readq = read * pow(2, 31 - SHIFT);
	int64_t resq = RES * pow(2, 31 - SHIFT);

	int32_t resultq = 
		(int32_t)((readq << (31 - SHIFT)) / resq);

	// resultq: 000b 8a3d
	// 0000 0000 0000 1011 1 . 000 1010 0011 1101
	// 10111 = 23
	// 000 1010 0011 1101 = 2^-4 + 2^-6, etc with -10, -11, -12, -13, -14, -16 = 0.080032349
	// 23 + 0.080032349

	printf("read: %d %x;\n", read, read);
	printf("readq: %lld %llx\n", readq, readq);
	printf("resq: %lld %llx\n", resq, resq);
	printf("resultq: %d %x;\n", resultq, resultq);
	printf("result: %s%d.%d;\n", PRIq_arg(resultq, 6, SHIFT));

	return 0;
}
