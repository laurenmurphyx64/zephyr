/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/common/instr_mem.h>
#include <zephyr/sys/word_granular.h>

void *arch_memset_i(void *buf, int c, size_t n)
{
#ifndef CONFIG_ARCH_HAS_WORD_GRANULAR_INSTR_MEM
	return memset(buf, c, n);
#else
	return memset_word_granular(buf, c, n);
#endif
}

void *arch_memcpy_d2i(void *d, const void *s, size_t n)
{
#ifndef CONFIG_ARCH_HAS_WORD_GRANULAR_INSTR_MEM
	return memcpy(d, s, n);
#else
	return memcpy_to_word_granular(d, s, n);
#endif
}

void *arch_memcpy_i2d(void *d, const void *s, size_t n)
{
#ifndef CONFIG_ARCH_HAS_WORD_GRANULAR_INSTR_MEM
	return memcpy(d, s, n);
#else
	return memcpy_from_word_granular(d, s, n);
#endif
}
