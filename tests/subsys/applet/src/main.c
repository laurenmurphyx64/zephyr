/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/word_granular_access.h>
#include <zephyr/sys/util.h>

static ZTEST_DMEM volatile int expected_reason = -1;

APPLET_THREAD_STACK_DEFINE(applet_1_stack, CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);
static struct applet applet_1;

APPLET_THREAD_STACK_DEFINE(applet_2_stack, CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);
static struct applet applet_2;

static void applet_before(void *f)
{
	ARG_UNUSED(f);
	expected_reason = -1;

	memset(&applet_1, 0, sizeof(applet_1));
	memset(&applet_1_stack, 0, CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);
	memset(&applet_2, 0, sizeof(applet_2));
	memset(&applet_2_stack, 0, CONFIG_APPLET_THREAD_STACK_SIZE_DEFAULT);
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *pEsf)
{
	ARG_UNUSED(pEsf);
	printk("Caught system error -- reason %d\n", reason);

	if (expected_reason == -1) {
		printk("Was not expecting a crash\n");
		ztest_test_fail();
		return;
	}

	if (reason != expected_reason) {
		printk("Wrong reason, got %d but expected %d\n", reason, expected_reason);
		ztest_test_fail();
		return;
	}

	expected_reason = -1;
}

ZTEST(applet, test_cpu_pinning)
{
	if (CONFIG_MP_MAX_NUM_CPUS < 2) {
		ztest_test_skip();
		return;
	}

	

	ztest_test_fail();
}

ZTEST_SUITE(applet, NULL, NULL, applet_before, NULL, NULL);
