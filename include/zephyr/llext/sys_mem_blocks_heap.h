/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_LLEXT_SYS_MEM_BLOCKS_HEAP_H
#define ZEPHYR_LLEXT_SYS_MEM_BLOCKS_HEAP_H

#ifndef CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS
#error "This header should not be included if CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS is not selected"
#endif

#include <zephyr/llext/heap.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct llext_sys_mem_blocks_heap {
	struct llext_heap heap;
};

int llext_sys_mem_blocks_heap_init_harvard(struct llext_heap *h, void *instr_mem,
					   size_t instr_bytes, void *data_mem, size_t data_bytes);
int llext_sys_mem_blocks_heap_init(struct llext_heap *h, void *mem, size_t bytes);
int llext_sys_mem_blocks_heap_uninit(struct llext_heap *h);
bool llext_sys_mem_blocks_heap_is_inited(struct llext_heap *h);
void llext_sys_mem_blocks_heap_reset(struct llext_heap *h, struct llext *ext);
void *llext_sys_mem_blocks_heap_alloc_metadata(struct llext_heap *h, size_t bytes);
void *llext_sys_mem_blocks_heap_aligned_alloc_data(struct llext_heap *h, struct llext *ext,
						   size_t align, size_t bytes);
void *llext_sys_mem_blocks_heap_aligned_alloc_instr(struct llext_heap *h, struct llext *ext,
						    size_t align, size_t bytes);
void llext_sys_mem_blocks_heap_free_metadata(struct llext_heap *h, void *ptr);
void llext_sys_mem_blocks_heap_free_data(struct llext_heap *h, struct llext *ext, void *ptr);
void llext_sys_mem_blocks_heap_free_instr(struct llext_heap *h, struct llext *ext, void *ptr);

#define Z_LLEXT_SYS_MEM_BLOCKS_HEAP()                                                              \
	{                                                                                          \
		.heap =                                                                            \
			{                                                                          \
				.init_harvard = llext_sys_mem_blocks_heap_init_harvard,            \
				.init = llext_sys_mem_blocks_heap_init,                            \
				.uninit = llext_sys_mem_blocks_heap_uninit,                        \
				.is_inited = llext_sys_mem_blocks_heap_is_inited,                  \
				.reset = llext_sys_mem_blocks_heap_reset,                          \
				.alloc_metadata = llext_sys_mem_blocks_heap_alloc_metadata,        \
				.aligned_alloc_data =                                              \
					llext_sys_mem_blocks_heap_aligned_alloc_data,              \
				.aligned_alloc_instr =                                             \
					llext_sys_mem_blocks_heap_aligned_alloc_instr,             \
				.free_metadata = llext_sys_mem_blocks_heap_free_metadata,          \
				.free_data = llext_sys_mem_blocks_heap_free_data,                  \
				.free_instr = llext_sys_mem_blocks_heap_free_instr,                \
			},                                                                         \
	}

extern struct llext_sys_mem_blocks_heap llext_sys_mem_blocks_heap_inst;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_LLEXT_SYS_MEM_BLOCKS_HEAP_H */
