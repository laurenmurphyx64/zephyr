/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/common/instr_mem.h>
#include <zephyr/sys/util.h>

__weak void *arch_memset_i(void *buf, int c, size_t n)
{
	return memset(buf, c, n);
}

__weak void *arch_memcpy_d2i(void *d, const void *s, size_t n)
{
	return memcpy(d, s, n);
}

__weak void *arch_memcpy_i2d(void *d, const void *s, size_t n)
{
	return memcpy(d, s, n);
}
