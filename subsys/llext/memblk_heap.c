/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/llext.h>
#include <zephyr/sys/mem_blocks.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(llext, CONFIG_LLEXT_LOG_LEVEL);

#include "llext_priv.h"

#ifdef CONFIG_HARVARD
BUILD_ASSERT(CONFIG_LLEXT_INSTR_HEAP_SIZE * KB(1) % CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE == 0,
	     "CONFIG_LLEXT_INSTR_HEAP_SIZE must be multiple of "
	     "CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE");
BUILD_ASSERT(CONFIG_LLEXT_DATA_HEAP_SIZE * KB(1) % CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE == 0,
	     "CONFIG_LLEXT_DATA_HEAP_SIZE must be multiple of "
	     "CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE");
BUILD_ASSERT(CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE % LLEXT_PAGE_SIZE == 0,
	     "CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE must be multiple of LLEXT_PAGE_SIZE");
uint8_t llext_instr_heap_buf[CONFIG_LLEXT_INSTR_HEAP_SIZE * KB(1)]
	__aligned(CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE)
	__attribute__((section(".rodata.llext_instr_heap")));
uint8_t llext_data_heap_buf[CONFIG_LLEXT_DATA_HEAP_SIZE * KB(1)]
	__aligned(CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE)
	__attribute__((section(".data.llext_data_heap")));
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(llext_instr_heap, CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
				   CONFIG_LLEXT_INSTR_HEAP_SIZE * KB(1) /
					   CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
				   llext_instr_heap_buf);
SYS_MEM_BLOCKS_DEFINE_WITH_EXT_BUF(llext_data_heap, CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
				   CONFIG_LLEXT_DATA_HEAP_SIZE * KB(1) /
					   CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
				   llext_data_heap_buf);
K_HEAP_DEFINE(llext_metadata_heap, CONFIG_LLEXT_METADATA_HEAP_SIZE * KB(1));
#else
BUILD_ASSERT(CONFIG_LLEXT_EXT_HEAP_SIZE * KB(1) % CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE == 0,
	     "CONFIG_LLEXT_EXT_HEAP_SIZE must be multiple of "
	     "CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE");
BUILD_ASSERT(CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE % LLEXT_PAGE_SIZE == 0,
	     "CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE must be multiple of LLEXT_PAGE_SIZE");
SYS_MEM_BLOCKS_DEFINE(llext_ext_heap, CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
		      CONFIG_LLEXT_EXT_HEAP_SIZE * KB(1) / CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE,
		      CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE);
K_HEAP_DEFINE(llext_metadata_heap, CONFIG_LLEXT_METADATA_HEAP_SIZE * KB(1));
#define llext_instr_heap    llext_ext_heap
#define llext_data_heap     llext_ext_heap
#define llext_metadata_heap llext_metadata_heap
#endif

static inline struct llext_alloc *get_llext_alloc(struct llext_alloc_map *map, void *alloc_ptr)
{
	for (int i = 0; i < map->idx; i++) {
		if (map->map[i].memblk_ptr && map->map[i].memblk_ptr[0] == alloc_ptr) {
			return &map->map[i];
		}
	}
	return NULL;
}

static void *llext_memblk_aligned_alloc_data_instr(struct llext *ext, sys_mem_blocks_t *memblk_heap,
						   size_t align, size_t bytes)
{
	if (ext->mem_alloc_map.idx >= LLEXT_MEM_COUNT) {
		return NULL;
	}

	if (CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE % align != 0) {
		LOG_ERR("Requested alignment %zu not possible with block alignment %d", align,
			CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE);
		return NULL;
	}

	int num = bytes / CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE +
		  ((bytes % CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE) ? 1 : 0);
	void **blocks = k_heap_alloc(&llext_metadata_heap, sizeof(void *) * num, K_NO_WAIT);

	if (blocks == NULL) {
		return NULL;
	}

	int ret = sys_mem_blocks_alloc_contiguous(memblk_heap, num, blocks);

	if (ret != 0) {
		return NULL;
	}

	struct llext_alloc *mem_alloc = &ext->mem_alloc_map.map[ext->mem_alloc_map.idx];

	mem_alloc->memblk_ptr = blocks;
	mem_alloc->num_blocks = num;
	ext->mem_alloc_map.idx++;

	return mem_alloc->memblk_ptr[0];
}

static void llext_memblk_free_data_instr(struct llext *ext, sys_mem_blocks_t *memblk_heap,
					 void *ptr)
{
	struct llext_alloc *mem_alloc = get_llext_alloc(&ext->mem_alloc_map, ptr);

	if (!mem_alloc) {
		LOG_ERR("Could not find sys_mem_blocks alloc to free pointer %p", ptr);
		return;
	}

	sys_mem_blocks_free_contiguous(memblk_heap, mem_alloc->memblk_ptr[0],
				       mem_alloc->num_blocks);
	k_heap_free(&llext_metadata_heap, mem_alloc->memblk_ptr);
	mem_alloc->num_blocks = 0;
	mem_alloc->memblk_ptr = NULL;
}

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

	return llext_memblk_aligned_alloc_data_instr(ext, &llext_data_heap, align, bytes);
}

void *llext_aligned_alloc_instr(struct llext *ext, size_t align, size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	return llext_memblk_aligned_alloc_data_instr(ext, &llext_instr_heap, align, bytes);
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

	llext_memblk_free_data_instr(ext, &llext_data_heap, ptr);
}

void llext_free_instr(struct llext *ext, void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

	llext_memblk_free_data_instr(ext, &llext_instr_heap, ptr);
}

void llext_heap_reset(struct llext *ext)
{
	ext->mem_alloc_map.idx = 0;
}
