/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/sys/word_granular.h>
#include <zephyr/sys/util.h>

static ZTEST_DMEM volatile int expected_reason = -1;

#define ARR_SIZE 16

uint32_t word_granular_buf[ARR_SIZE] __aligned(4)
__attribute__((section(CONFIG_TEST_WORD_GRANULAR_SECTION), used)) = {0};

/* Below arrays should be placed in non-word-granular memory */
uint32_t non_word_granular_buf[ARR_SIZE] __aligned(4) = {0};

uint32_t expected_src_buf[ARR_SIZE] __aligned(4) = {0};

uint32_t expected_dst_buf[ARR_SIZE] __aligned(4) = {0};

static const uint32_t init_template[ARR_SIZE] __aligned(4) = {
	0x12345678U, 0xABCDEF01U, 0xDEADBEEFU, 0x0D15EA5EU, 0xAAAAAAAAU, 0xAAAAAAAAU,
	0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU,
	0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU, 0xAAAAAAAAU};

#define CHECK_DATA_MEMCPY(actual, expected, op, src, dst, len)                                     \
	do {                                                                                       \
		const uint32_t *actual_ = (actual);                                                \
		const uint32_t *expected_ = (expected);                                            \
		const char *op_ = (op);                                                            \
		const void *src_ = (src);                                                          \
		const void *dst_ = (dst);                                                          \
		const size_t len_ = (len);                                                         \
		for (size_t i_ = 0; i_ < (ARR_SIZE); i_++) {                                       \
			zassert_equal(actual_[i_], expected_[i_],                                  \
				      "%s: from %p to %p, length %d: mismatch at %p: expected "    \
				      "0x%08X, got 0x%08X",                                        \
				      op_, src_, dst_, len_, (void *)(actual_ + i_),               \
				      expected_[i_], actual_[i_]);                                 \
		}                                                                                  \
	} while (0)

#define CHECK_DATA_MEMSET(actual, expected, op, val, dst, len)                                     \
	do {                                                                                       \
		const uint32_t *actual_ = (actual);                                                \
		const uint32_t *expected_ = (expected);                                            \
		const char *op_ = (op);                                                            \
		const int val_ = (val);                                                            \
		const void *dst_ = (dst);                                                          \
		const size_t len_ = (len);                                                         \
		for (size_t i_ = 0; i_ < (ARR_SIZE); i_++) {                                       \
			zassert_equal(actual_[i_], expected_[i_],                                  \
				      "%s: set %d at %p, length %d: mismatch at %p: expected "     \
				      "0x%08X, got 0x%08X",                                        \
				      op_, val_, dst_, len_, (void *)(actual_ + i_),               \
				      expected_[i_], actual_[i_]);                                 \
		}                                                                                  \
	} while (0)

static void lib_word_granular_before(void *f)
{
	ARG_UNUSED(f);
	expected_reason = -1;
	for (int i = 0; i < ARRAY_SIZE(word_granular_buf); i++) {
		word_granular_buf[i] = init_template[i];
		non_word_granular_buf[i] = init_template[i];
		expected_src_buf[i] = init_template[i];
		expected_dst_buf[i] = init_template[i];
	}
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *pEsf)
{
	ARG_UNUSED(pEsf);
	printk("Caught system error -- reason %d\n", reason);

	if (expected_reason == -1) {
		printk("Was not expecting a crash\n");
		ztest_test_fail();
	}

	if (reason != expected_reason) {
		printk("Wrong reason, got %d but expected %d\n", reason, expected_reason);
		ztest_test_fail();
	}

	expected_reason = -1;
	ztest_test_pass();
}

ZTEST(lib_word_granular, test_mem_is_word_granular)
{
	/* write a byte in non-word-granular memory; should not crash */
	((uint8_t *)non_word_granular_buf)[1] = 0xFF;

	/* write a byte in word-granular memory; should crash */
	expected_reason = K_ERR_CPU_EXCEPTION;
	((uint8_t *)word_granular_buf)[1] = 0xFF;

	/* test should raise an exception and should not reach this line */
	printk("test buffer not in word-granular memory; byte write did not cause exception\n");
	ztest_test_fail();
}

ZTEST(lib_word_granular, test_memset_word_granular)
{
	uint8_t *word_granular_ptr;

	/* adjacent byte preservation */
	memset((uint8_t *)(expected_dst_buf + 4) + 2, 0xAB, 1);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 4) + 2;
	memset_word_granular(word_granular_ptr, 0xAB, 1);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0xAB, word_granular_ptr,
			  1);

	/* word-aligned destination, word-aligned end */
	memset((uint8_t *)(expected_dst_buf + 5), 0xCD, 8);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 5);
	memset_word_granular(word_granular_ptr, 0xCD, 8);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0xCD, word_granular_ptr,
			  8);

	/* word-aligned destination, length is 1 byte under word boundary */
	memset((uint8_t *)(expected_dst_buf + 7), 0xEF, 7);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 7);
	memset_word_granular(word_granular_ptr, 0xEF, 7);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0xEF, word_granular_ptr,
			  7);

	/* half-aligned destination, length is 1 byte over word boundary */
	memset((uint8_t *)(expected_dst_buf + 9) + 2, 0x12, 9);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 9) + 2;
	memset_word_granular(word_granular_ptr, 0x12, 9);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0x12, word_granular_ptr,
			  9);

	/* byte-aligned destination, spanning two words */
	memset((uint8_t *)(expected_dst_buf + 12) + 1, 0x34, 4);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 12) + 1;
	memset_word_granular(word_granular_ptr, 0x34, 4);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0x34, word_granular_ptr,
			  4);

	/* zero length */
	memset_word_granular(word_granular_buf, 0x00, 0);
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0x00, word_granular_buf,
			  0);

	/* fill buffer with repeating pattern */
	memset(expected_dst_buf, 0xAB, ARR_SIZE * sizeof(uint32_t));
	memset_word_granular(word_granular_buf, 0xAB, ARR_SIZE * sizeof(uint32_t));
	CHECK_DATA_MEMSET(word_granular_buf, expected_dst_buf, "memset", 0xAB, word_granular_buf,
			  ARR_SIZE * sizeof(uint32_t));
}

ZTEST(lib_word_granular, test_memcpy_to_word_granular)
{
	uint8_t *non_word_granular_ptr;
	uint8_t *word_granular_ptr;

	/* adjacent byte preservation */
	memcpy((uint8_t *)(expected_dst_buf + 4) + 3, (uint8_t *)expected_src_buf + 1, 1);
	non_word_granular_ptr = (uint8_t *)non_word_granular_buf + 1;
	word_granular_ptr = (uint8_t *)(word_granular_buf + 4) + 3;
	memcpy_to_word_granular(word_granular_ptr, non_word_granular_ptr, 1);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 1);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 1);

	/* word-aligned source and destination, length 1 byte over word boundary */
	memcpy((uint8_t *)(expected_dst_buf + 5), (uint8_t *)(expected_src_buf + 1), 9);
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 1);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 5);
	memcpy_to_word_granular(word_granular_ptr, non_word_granular_ptr, 9);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 9);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 9);

	/* half-aligned source and destination, length 1 byte under word boundary */
	memcpy((uint8_t *)(expected_dst_buf + 7) + 2, (uint8_t *)(expected_src_buf + 2) + 2, 9);
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 2) + 2;
	word_granular_ptr = (uint8_t *)(word_granular_buf + 7) + 2;
	memcpy_to_word_granular(word_granular_ptr, non_word_granular_ptr, 9);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 9);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 9);

	/* byte-aligned source and destination, spanning two words */
	memcpy((uint8_t *)(expected_dst_buf + 9) + 1, (uint8_t *)(expected_src_buf + 3) + 3, 6);
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 3) + 3;
	word_granular_ptr = (uint8_t *)(word_granular_buf + 9) + 1;
	memcpy_to_word_granular(word_granular_ptr, non_word_granular_ptr, 6);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 6);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, 6);

	/* zero length */
	memcpy_to_word_granular(word_granular_buf, non_word_granular_buf, 0);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_buf,
			  word_granular_buf, 0);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_buf,
			  word_granular_buf, 0);

	/* stagger copy most of buffer */
	memcpy((uint8_t *)expected_dst_buf + 3, (uint8_t *)expected_src_buf + 1,
	       (ARR_SIZE * sizeof(uint32_t)) - 4);
	non_word_granular_ptr = (uint8_t *)non_word_granular_buf + 1;
	word_granular_ptr = (uint8_t *)(word_granular_buf) + 3;
	memcpy_to_word_granular(word_granular_ptr, non_word_granular_ptr,
				(ARR_SIZE * sizeof(uint32_t)) - 4);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_src_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, (ARR_SIZE * sizeof(uint32_t)) - 4);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_dst_buf, "memcpy", non_word_granular_ptr,
			  word_granular_ptr, (ARR_SIZE * sizeof(uint32_t)) - 4);
}

ZTEST(lib_word_granular, test_memcpy_from_word_granular)
{
	uint8_t *word_granular_ptr;
	uint8_t *non_word_granular_ptr;

	/* adjacent byte preservation */
	memcpy((uint8_t *)(expected_dst_buf + 4) + 3, (uint8_t *)expected_src_buf + 1, 1);
	word_granular_ptr = (uint8_t *)word_granular_buf + 1;
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 4) + 3;
	memcpy_from_word_granular(non_word_granular_ptr, word_granular_ptr, 1);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 1);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 1);

	/* word-aligned source and destination, length 1 byte under word boundary */
	memcpy((uint8_t *)(expected_dst_buf + 5), (uint8_t *)(expected_src_buf + 1), 7);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 1);
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 5);
	memcpy_from_word_granular(non_word_granular_ptr, word_granular_ptr, 7);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 7);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 7);

	/* half-aligned source and destination, length 1 byte over word boundary */
	memcpy((uint8_t *)(expected_dst_buf + 7) + 2, (uint8_t *)(expected_src_buf + 2) + 2, 7);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 2) + 2;
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 7) + 2;
	memcpy_from_word_granular(non_word_granular_ptr, word_granular_ptr, 7);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 7);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 7);

	/* byte-aligned source and destination, spanning two words */
	memcpy((uint8_t *)(expected_dst_buf + 9) + 1, (uint8_t *)(expected_src_buf + 3) + 3, 6);
	word_granular_ptr = (uint8_t *)(word_granular_buf + 3) + 3;
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf + 9) + 1;
	memcpy_from_word_granular(non_word_granular_ptr, word_granular_ptr, 6);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 6);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, 6);

	/* zero length */
	memcpy_from_word_granular(non_word_granular_buf, word_granular_buf, 0);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_buf,
			  non_word_granular_buf, 0);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_buf,
			  non_word_granular_buf, 0);

	/* stagger copy most of buffer */
	memcpy((uint8_t *)expected_dst_buf + 3, (uint8_t *)expected_src_buf + 1,
	       (ARR_SIZE * sizeof(uint32_t)) - 4);
	word_granular_ptr = (uint8_t *)word_granular_buf + 1;
	non_word_granular_ptr = (uint8_t *)(non_word_granular_buf) + 3;
	memcpy_from_word_granular(non_word_granular_ptr, word_granular_ptr,
				  (ARR_SIZE * sizeof(uint32_t)) - 4);
	CHECK_DATA_MEMCPY(word_granular_buf, expected_src_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, (ARR_SIZE * sizeof(uint32_t)) - 4);
	CHECK_DATA_MEMCPY(non_word_granular_buf, expected_dst_buf, "memcpy", word_granular_ptr,
			  non_word_granular_ptr, (ARR_SIZE * sizeof(uint32_t)) - 4);
}

ZTEST_SUITE(lib_word_granular, NULL, NULL, lib_word_granular_before, NULL, NULL);
