/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_COMMON_INSTR_MEM_H_
#define ZEPHYR_INCLUDE_ARCH_COMMON_INSTR_MEM_H_

#include <zephyr/sys/util.h>

/**
 * @file
 *
 * @brief Public instruction memory APIs.
 * @defgroup instr_mem Instruction Memory APIs
 * @ingroup arch-interface
 * @{
 */

/**
 * @brief Memset buffer in instruction memory
 *
 * Memset buffer in instruction memory.
 *
 * @param buf Buffer to memset
 * @param c Value to set
 * @param n Number of bytes to set
 * @returns the provided buffer pointer, or `NULL` on error
 */
void *arch_memset_i(void *buf, int c, size_t n);

/**
 * @brief Memcpy buffer from data memory to instruction memory
 *
 * Memcpy used to copy buffer from data memory to instruction memory.
 *
 * @param d Destination buffer
 * @param s Source buffer
 * @param n Number of bytes to copy
 * @returns the provided destination buffer pointer, or `NULL` on error
 */
void *arch_memcpy_d2i(void *d, const void *s, size_t n);

/**
 * @brief Memcpy buffer from instruction memory to data memory
 *
 * Memcpy used to copy buffer from instruction memory to data memory.
 *
 * @param d Destination buffer
 * @param s Source buffer
 * @param n Number of bytes to copy
 * @returns the provided destination buffer pointer, or `NULL` on error
 */
void *arch_memcpy_i2d(void *d, const void *s, size_t n);

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_ARCH_COMMON_INSTR_MEM_H_ */
