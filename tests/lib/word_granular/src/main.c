/*
 * Copyright (c) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zephyr/sys/word_granular.h>
#include <zephyr/sys/util.h>

static ZTEST_DMEM volatile int expected_reason = -1;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *pEsf)
{
	printk("Caught system error -- reason %d\n", reason);

	if (expected_reason == -1) {
		printk("Was not expecting a crash\n");
		ztest_test_fail();
	}

	if (reason != expected_reason) {
		printk("Wrong crash type got %d expected %d\n", reason,
		       expected_reason);
		ztest_test_fail();
	}

	expected_reason = -1;
	ztest_test_pass();
}

ZTEST(lib_mem_block, test_mem_is_word_granular)
{
	/* test should raise an exception and should not reach this line */
	ztest_test_fail();
}

ZTEST(lib_mem_block, test_memcpy_to_word_granular)
{
	
}

ZTEST(lib_mem_block, test_memcpy_from_word_granular)
{
	
}

ZTEST(lib_mem_block, test_memset_word_granular)
{

}

static void *lib_word_granular_setup(void)
{
	return NULL;
}

ZTEST_SUITE(lib_mem_block, NULL, lib_word_granular_setup, NULL, NULL, NULL);
