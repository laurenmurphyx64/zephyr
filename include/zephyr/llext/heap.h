/*
 * Copyright (c) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_LLEXT_HEAP_H
#define ZEPHYR_LLEXT_HEAP_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * @brief LLEXT heap functions
 *
 * The following functions are used to manage heap memory
 * for the \ref llext subsystem. Include the appropriate llext heap
 * implementation header (e.g., @ref llext/k_heap.h or
 * @ref llext/sys_mem_blocks_heap.h) to select the desired heap
 * implementation.
 *
 * @defgroup llext_heap_apis LLEXT heap API
 * @ingroup llext_apis
 * @{
 */

#include <zephyr/llext/llext.h>

/** @cond ignore */
struct llext_heap {
	int (*init_harvard)(struct llext_heap *h, void *instr_mem, size_t instr_bytes,
			    void *data_mem, size_t data_bytes);

	int (*init)(struct llext_heap *h, void *mem, size_t bytes);

	int (*uninit)(struct llext_heap *h);

	bool (*is_inited)(struct llext_heap *h);

	void (*reset)(struct llext_heap *h, struct llext *ext);

	void *(*alloc_metadata)(struct llext_heap *h, size_t bytes);

	void *(*aligned_alloc_data)(struct llext_heap *h, struct llext *ext, size_t align,
				    size_t bytes);

	void *(*aligned_alloc_instr)(struct llext_heap *h, struct llext *ext, size_t align,
				     size_t bytes);

	void (*free_metadata)(struct llext_heap *h, void *ptr);

	void (*free_data)(struct llext_heap *h, struct llext *ext, void *ptr);

	void (*free_instr)(struct llext_heap *h, struct llext *ext, void *ptr);
};

extern struct llext_heap *llext_heap_inst;
/* @endcond */

/**
 * @brief Dynamically initialize the llext heap on a Harvard architecture platform
 *
 * @param instr_mem Pointer to instruction memory buffer
 * @param instr_bytes Size of the instruction memory buffer
 * @param data_mem Pointer to data memory buffer
 * @param data_bytes Size of the data memory buffer
 *
 * @return 0 on success, negative error code on failure
 */
static inline int llext_heap_init_harvard(void *instr_mem, size_t instr_bytes, void *data_mem,
					  size_t data_bytes)
{
	return llext_heap_inst->init_harvard(llext_heap_inst, instr_mem, instr_bytes, data_mem,
					     data_bytes);
}

/**
 * @brief Dynamically initialize the llext heap
 *
 * @param mem Pointer to memory buffer
 * @param bytes Size of the memory buffer
 *
 * @return 0 on success, negative error code on failure
 */
static inline int llext_heap_init(void *mem, size_t bytes)
{
	return llext_heap_inst->init(llext_heap_inst, mem, bytes);
}

/**
 * @brief Dynamically uninitialize the llext heap
 *
 * @return 0 on success, negative error code on failure
 */
static inline int llext_heap_uninit(void)
{
	return llext_heap_inst->uninit(llext_heap_inst);
}

/** @cond ignore */
static inline bool llext_heap_is_inited(void)
{
	return llext_heap_inst->is_inited(llext_heap_inst);
}

static inline void llext_heap_reset(struct llext *ext)
{
	if (llext_heap_inst->reset) {
		llext_heap_inst->reset(llext_heap_inst, ext);
	}
}

static inline void *llext_heap_alloc_metadata(size_t bytes)
{
	return llext_heap_inst->alloc_metadata(llext_heap_inst, bytes);
}

static inline void *llext_heap_aligned_alloc_data(struct llext *ext, size_t align, size_t bytes)
{
	return llext_heap_inst->aligned_alloc_data(llext_heap_inst, ext, align, bytes);
}

static inline void *llext_heap_aligned_alloc_instr(struct llext *ext, size_t align, size_t bytes)
{
	return llext_heap_inst->aligned_alloc_instr(llext_heap_inst, ext, align, bytes);
}

static inline void llext_heap_free_metadata(void *ptr)
{
	llext_heap_inst->free_metadata(llext_heap_inst, ptr);
}

static inline void llext_heap_free_data(struct llext *ext, void *ptr)
{
	llext_heap_inst->free_data(llext_heap_inst, ext, ptr);
}

static inline void llext_heap_free_instr(struct llext *ext, void *ptr)
{
	llext_heap_inst->free_instr(llext_heap_inst, ext, ptr);
}
/* @endcond */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_LLEXT_HEAP_H */
