/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "llext_priv.h"

#ifdef CONFIG_LLEXT_HEAP_DYNAMIC
#ifdef CONFIG_HARVARD
struct k_heap llext_instr_heap;
struct k_heap llext_data_heap;
#define llext_metadata_heap llext_data_heap
#else
struct k_heap llext_heap;
#define llext_instr_heap    llext_heap
#define llext_data_heap     llext_heap
#define llext_metadata_heap llext_heap
#endif /* CONFIG_HARVARD */
#else  /* !CONFIG_LLEXT_HEAP_DYNAMIC */
#ifdef CONFIG_HARVARD
Z_HEAP_DEFINE_IN_SECT(llext_instr_heap, (CONFIG_LLEXT_INSTR_HEAP_SIZE * KB(1)),
		      __attribute__((section(".rodata.llext_instr_heap"))));
Z_HEAP_DEFINE_IN_SECT(llext_data_heap, (CONFIG_LLEXT_DATA_HEAP_SIZE * KB(1)),
		      __attribute__((section(".data.llext_data_heap"))));
#define llext_metadata_heap llext_data_heap
#else
K_HEAP_DEFINE(llext_heap, CONFIG_LLEXT_HEAP_SIZE * KB(1));
#define llext_instr_heap    llext_heap
#define llext_data_heap     llext_heap
#define llext_metadata_heap llext_heap
#endif /* CONFIG_HARVARD */
#endif /* CONFIG_LLEXT_HEAP_DYNAMIC */

void *llext_alloc_metadata(size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	return k_heap_alloc(&llext_metadata_heap, bytes, K_NO_WAIT);
}

void *llext_aligned_alloc_data(struct llext *ext, size_t align, size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	return k_heap_aligned_alloc(&llext_data_heap, align, bytes, K_NO_WAIT);
}

void *llext_aligned_alloc_instr(struct llext *ext, size_t align, size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	return k_heap_aligned_alloc(&llext_instr_heap, align, bytes, K_NO_WAIT);
}

void llext_free_metadata(void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

	k_heap_free(&llext_metadata_heap, ptr);
}

void llext_free_data(struct llext *ext, void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

	k_heap_free(&llext_data_heap, ptr);
}

void llext_free_instr(struct llext *ext, void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

	k_heap_free(&llext_instr_heap, ptr);
}

void llext_heap_reset(struct llext *ext)
{
	ARG_UNUSED(ext);
}
