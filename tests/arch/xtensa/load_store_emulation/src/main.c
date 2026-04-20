/*
 * Copyright (c) 2024, Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <string.h>

#include <zephyr/sys/util.h>

uint32_t test_data[8] __attribute__((section(".iram.data.test_data"), aligned(4))) =
	 {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

static const uint32_t test_data_init_template[8] = {
	0x12345678, 0xABCDEF01, 0xDEADBEEF, 0x0D15EA5E,
	0x0, 0x0, 0x0, 0x0,
};

static void load_store_emulation_before(void *f)
{
	ARG_UNUSED(f);
	memcpy(test_data, test_data_init_template, sizeof(test_data));
}

ZTEST(load_store_emulation, test_unsupported_load)
{
	uint8_t *byte_ptr = (uint8_t *)test_data;
	uint16_t *half_ptr = (uint16_t *)test_data;
	uint32_t *word_ptr = (uint32_t *)test_data;

	/* Byte loads using first word */
	for (int i = 0; i < 4; i++) {
		uint8_t expected = (test_data[0] >> (i * 8)) & 0xFF;
		zassert_equal(byte_ptr[i], expected,
			      "byte load mismatch at %p: expected 0x%02X, got 0x%02X",
			      (void *)(byte_ptr + i), expected, byte_ptr[i]);
	}

	/* Half word load using second word */
	for (int i = 0; i < 2; i++) {
		uint16_t expected = (test_data[1] >> (i * 16)) & 0xFFFF;
		zassert_equal(half_ptr[i + 2], expected, 
			      "half word load mismatch at %p: expected 0x%04X, got 0x%04X",
			      (void *)(half_ptr + i + 2), expected, half_ptr[i + 2]);
	}

	/* Unaligned half word load using third word */
	uint16_t half_ptr_unaligned = *(uint16_t *)(byte_ptr + 13);
	uint16_t expected_half_unaligned = (test_data[3] >> 8) & 0xFFFF;
	zassert_equal(half_ptr_unaligned, expected_half_unaligned,
		      "unaligned half word load mismatch at %p: expected 0x%04X, got 0x%04X",
		      (void *)(byte_ptr + 13), expected_half_unaligned, half_ptr_unaligned);

	/* Word load using first four words */
	for (int i = 0; i < 4; i++) {
		uint32_t expected = test_data[i];
		zassert_equal(word_ptr[i], expected,
			      "word load mismatch at %p: expected 0x%08X, got 0x%08X",
			      (void *)(word_ptr + i), expected, word_ptr[i]);
	}

	/* Unaligned word load using third and fourth words */
	uint32_t word_ptr_unaligned = *(uint32_t *)(byte_ptr + 9);
	uint32_t expected_word_unaligned = (test_data[2] >> 8) | ((test_data[3] & 0xFF) << 24);
	zassert_equal(word_ptr_unaligned, expected_word_unaligned,
		      "unaligned word load mismatch at %p: expected 0x%08X, got 0x%08X",
		      (void *)(byte_ptr + 9), expected_word_unaligned, word_ptr_unaligned);

}

ZTEST(load_store_emulation, test_unsupported_store)
{
	/* Byte stores using first word */
	uint8_t *byte_ptr_src = (uint8_t *)test_data;
	uint8_t *byte_ptr_dst = (uint8_t *)(test_data + 4);
	for (int i = 0; i < 4; i++) {
		byte_ptr_dst[i] = byte_ptr_src[i];
	}
	for (int i = 0; i < 4; i++) {
		uint8_t expected_byte = (test_data[0] >> (i * 8)) & 0xFF;
		uint8_t actual_byte = (test_data[4] >> (i * 8)) & 0xFF;
		zassert_equal(byte_ptr_dst[i], expected_byte,
			      "byte store mismatch at %p: expected 0x%02X, got 0x%02X",
			      (void *)(byte_ptr_dst + i), expected_byte, actual_byte);
	}

	/* Half word store using second word */
	uint16_t *half_ptr_src = (uint16_t *)(test_data + 1);
	uint16_t *half_ptr_dst = (uint16_t *)(test_data + 6);
	for (int i = 0; i < 2; i++) {
		half_ptr_dst[i] = half_ptr_src[i];
	}
	for (int i = 0; i < 2; i++) {
		uint16_t expected_half = (test_data[1] >> (i * 16)) & 0xFFFF;
		uint16_t actual_half = (test_data[5] >> ((i % 2) * 16)) & 0xFFFF;
		zassert_equal(half_ptr_dst[i], expected_half,
			      "half word store mismatch at %p: expected 0x%04X, got 0x%04X",
			      (void *)(half_ptr_dst + i), expected_half, actual_half);
	}

	/* Unaligned half word store using third word */
	uint16_t half_ptr_unaligned_src = *(uint16_t *)(byte_ptr_src + 13);
	uint16_t *half_ptr_unaligned_dst = (uint16_t *)(byte_ptr_dst + 13);
	uint16_t expected_half_unaligned = (test_data[3] >> 8) & 0xFFFF;
	*half_ptr_unaligned_dst = half_ptr_unaligned_src;
	uint16_t actual_half_unaligned = (test_data[6] >> 8) & 0xFFFF;
	
	zassert_equal(*half_ptr_unaligned_dst, expected_half_unaligned,
		      "unaligned half word store mismatch at %p: expected 0x%04X, got 0x%04X",
		      (void *)(byte_ptr_dst + 13), expected_half_unaligned, actual_half_unaligned);

	/* Word store using first four words; overwrite previous */
	uint32_t *word_ptr_src = (uint32_t *)test_data;
	uint32_t *word_ptr_dst = (uint32_t *)(test_data + 4);
	for (int i = 0; i < 4; i++) {
		word_ptr_dst[i] = word_ptr_src[i];
	}
	for (int i = 0; i < 4; i++) {
		zassert_equal(word_ptr_dst[i], word_ptr_src[i],
			      "word store mismatch at %p: expected 0x%08X, got 0x%08X",
			      (void *)(word_ptr_dst + i), word_ptr_src[i], word_ptr_dst[i]);
	}

	/* Unaligned word store using third and fourth words */
	uint32_t *word_ptr_unaligned_src = (uint32_t *)(byte_ptr_src + 9);
	uint32_t *word_ptr_unaligned_dst = (uint32_t *)(byte_ptr_dst + 9);
	uint32_t expected_word_unaligned = (test_data[2] >> 8) | ((test_data[3] & 0xFF) << 24);
	*word_ptr_unaligned_dst = *word_ptr_unaligned_src;
	uint32_t actual_word_unaligned = (test_data[6] >> 8) | ((test_data[7] & 0xFF) << 24);
	zassert_equal(*word_ptr_unaligned_dst, expected_word_unaligned,
		      "unaligned word load mismatch at %p: expected 0x%08X, got 0x%08X",
		      word_ptr_unaligned_dst, expected_word_unaligned, actual_word_unaligned);
}

ZTEST(load_store_emulation, test_memset_memcpy)
{
	/* memcpy with unaligned destination and source */
	uint8_t *byte_ptr = (uint8_t *)test_data;
	memcpy(byte_ptr + 18, byte_ptr + 1, 13);

	zassert_equal(test_data[4], 0x34560000,
		"memcpy failed; expected 0x34560000 at %p, got 0x%08X", test_data + 4, test_data[4]);
	zassert_equal(test_data[5], 0xCDEF0112,
		"memcpy failed; expected 0xCDEF0112 at %p, got 0x%08X", test_data + 5, test_data[5]);
	zassert_equal(test_data[6], 0xADBEEFAB,
		"memcpy failed; expected 0xADBEEFAB at %p, got 0x%08X", test_data + 6, test_data[6]);
	zassert_equal(test_data[7], 0x00EA5EDE,
		"memcpy failed; expected 0x00EA5EDE at %p, got 0x%08X", test_data + 7, test_data[7]);

	/* memset with unaligned destination and source */
	memset(byte_ptr + 1, 0xAA, 13);
	zassert_equal(test_data[0], 0xAAAAAA78,
		"memset failed; expected 0xAAAAAA78 at %p, got 0x%08X", test_data, test_data[0]);
	zassert_equal(test_data[1], 0xAAAAAAAA,
		"memset failed; expected 0xAAAAAAAA at %p, got 0x%08X", test_data + 1, test_data[1]);
	zassert_equal(test_data[2], 0xAAAAAAAA,
		"memset failed; expected 0xAAAAAAAA at %p, got 0x%08X", test_data + 2, test_data[2]);
	zassert_equal(test_data[3], 0x0D15AAAA,
		"memset failed; expected 0x0D15AAAA at %p, got 0x%08X", test_data + 3, test_data[3]);
}

ZTEST_SUITE(load_store_emulation, NULL, NULL, load_store_emulation_before, NULL, NULL);
