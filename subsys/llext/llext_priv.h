/*
 * Copyright (c) 2024 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_LLEXT_PRIV_H_
#define ZEPHYR_SUBSYS_LLEXT_PRIV_H_

#include <zephyr/kernel.h>
#include <zephyr/llext/llext.h>
#include <zephyr/llext/llext_internal.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/mem_blocks.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/*
 * Macro to determine if section / region is in instruction memory
 * Will need to be updated if any non-ARC boards using Harvard architecture is added
 */
#if CONFIG_HARVARD && CONFIG_ARC
#define IN_NODE(inst, compat, base_addr, alloc)                                                    \
	(((uintptr_t)(base_addr) >= DT_REG_ADDR(DT_INST(inst, compat)) &&                          \
	  (uintptr_t)(base_addr + alloc) <=                                                        \
		  DT_REG_ADDR(DT_INST(inst, compat)) + DT_REG_SIZE(DT_INST(inst, compat)))) ||
#define INSTR_FETCHABLE(base_addr, alloc)                                                          \
	(DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(arc_iccm, IN_NODE, base_addr, alloc) false)
#elif CONFIG_HARVARD && !CONFIG_ARC
/* Unknown if section / region is in instruction memory; warn or compensate */
#define INSTR_FETCHABLE(base_addr, alloc) false
#else /* all non-Harvard architectures */
#define INSTR_FETCHABLE(base_addr, alloc) true
#endif

#ifdef CONFIG_MMU_PAGE_SIZE
#define LLEXT_PAGE_SIZE CONFIG_MMU_PAGE_SIZE
#elif CONFIG_ARC_MPU_VER == 2
#define LLEXT_PAGE_SIZE 2048
#else
/* Arm and non-v2 ARC MPUs want a 32 byte minimum MPU region */
#define LLEXT_PAGE_SIZE 32
#endif

/*
 * Global extension list
 */

extern sys_slist_t llext_list;
extern struct k_mutex llext_lock;

/*
 * Memory management (llext_mem.c)
 */

int llext_copy_strings(struct llext_loader *ldr, struct llext *ext,
		       const struct llext_load_param *ldr_parm);
int llext_copy_regions(struct llext_loader *ldr, struct llext *ext,
		       const struct llext_load_param *ldr_parm);
void llext_free_regions(struct llext *ext);
void llext_adjust_mmu_permissions(struct llext *ext);

#ifdef CONFIG_LLEXT_HEAP_K_HEAP
#ifdef CONFIG_HARVARD
extern struct k_heap llext_instr_heap;
extern struct k_heap llext_data_heap;
#define llext_metadata_heap llext_data_heap
#else
extern struct k_heap llext_heap;
#define llext_instr_heap llext_heap
#define llext_data_heap  llext_heap
#define llext_metadata_heap llext_heap
#endif
#elif defined(CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS)
#ifdef CONFIG_HARVARD
extern struct sys_mem_blocks llext_instr_heap;
extern struct sys_mem_blocks llext_data_heap;
extern struct k_heap llext_metadata_heap;
#else
extern struct sys_mem_blocks llext_heap;
#define llext_instr_heap llext_heap
#define llext_data_heap  llext_heap
extern struct k_heap llext_metadata_heap;
#endif
#else
#error "No LLEXT heap implementation selected; check CONFIG_LLEXT_HEAP_MANAGEMENT"
#endif

static inline bool llext_heap_is_inited(void)
{
#ifdef CONFIG_LLEXT_HEAP_DYNAMIC
	extern bool llext_heap_inited;

	return llext_heap_inited;
#else
	return true;
#endif
}

#ifdef CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS
static inline struct llext_alloc *get_llext_alloc(struct llext_alloc_map *map, void *alloc_ptr)
{
	for (int i = 0; i < map->idx; i++) {
		if (map->map[i].sys_mem_blocks_ptr &&
		    map->map[i].sys_mem_blocks_ptr[0] == alloc_ptr) {
			return &map->map[i];
		}
	}
	return NULL;
}

static void *llext_sys_mem_blocks_aligned_alloc_data_instr(struct llext *ext,
							   sys_mem_blocks_t *sys_mem_blocks_heap,
							   size_t align, size_t bytes)
{
	if (ext->mem_alloc_map.idx >= LLEXT_MEM_COUNT) {
		return NULL;
	}

	if (CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS_BLOCK_SIZE % align != 0) {
		LOG_ERR("Requested alignment %zu not possible with block alignment %d", align,
			CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS_BLOCK_SIZE);
		return NULL;
	}

	int num = bytes / CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS_BLOCK_SIZE +
		  ((bytes % CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS_BLOCK_SIZE) ? 1 : 0);
	void **blocks = k_heap_alloc(&llext_metadata_heap, sizeof(void *) * num, K_NO_WAIT);

	if (blocks == NULL) {
		return NULL;
	}

	int ret = sys_mem_blocks_alloc_contiguous(sys_mem_blocks_heap, num, blocks);

	if (ret != 0) {
		return NULL;
	}

	struct llext_alloc *mem_alloc = &ext->mem_alloc_map.map[ext->mem_alloc_map.idx];

	mem_alloc->sys_mem_blocks_ptr = blocks;
	mem_alloc->num_blocks = num;
	ext->mem_alloc_map.idx++;

	return mem_alloc->sys_mem_blocks_ptr[0];
}

static void llext_sys_mem_blocks_free_data_instr(struct llext *ext,
						 sys_mem_blocks_t *sys_mem_blocks_heap, void *ptr)
{
	struct llext_alloc *mem_alloc = get_llext_alloc(&ext->mem_alloc_map, ptr);

	if (!mem_alloc) {
		LOG_ERR("Could not find sys_mem_blocks alloc to free pointer %p", ptr);
		return;
	}

	sys_mem_blocks_free_contiguous(sys_mem_blocks_heap, mem_alloc->sys_mem_blocks_ptr[0],
				       mem_alloc->num_blocks);
	k_heap_free(&llext_metadata_heap, mem_alloc->sys_mem_blocks_ptr);
	mem_alloc->num_blocks = 0;
	mem_alloc->sys_mem_blocks_ptr = NULL;
}
#endif

static inline void *llext_alloc_metadata(size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	return k_heap_alloc(&llext_metadata_heap, bytes, K_NO_WAIT);
}

static inline void *llext_aligned_alloc_data(struct llext *ext, size_t align, size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

	/* Used for non-executable section */
#ifdef CONFIG_LLEXT_HEAP_K_HEAP
	return k_heap_aligned_alloc(&llext_data_heap, align, bytes, K_NO_WAIT);
#else
	return llext_sys_mem_blocks_aligned_alloc_data_instr(ext, &llext_data_heap, align, bytes);
#endif
}

static inline void *llext_aligned_alloc_instr(struct llext *ext, size_t align, size_t bytes)
{
	if (bytes == 0) {
		return NULL;
	}

	if (!llext_heap_is_inited()) {
		return NULL;
	}

#ifdef CONFIG_LLEXT_HEAP_K_HEAP
	return k_heap_aligned_alloc(&llext_instr_heap, align, bytes, K_NO_WAIT);
#else
	return llext_sys_mem_blocks_aligned_alloc_data_instr(ext, &llext_instr_heap, align, bytes);
#endif
}

static inline void llext_free_metadata(void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

	k_heap_free(&llext_metadata_heap, ptr);
}

static inline void llext_free_data(struct llext *ext, void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

#ifdef CONFIG_LLEXT_HEAP_K_HEAP
	k_heap_free(&llext_data_heap, ptr);
#else
	llext_sys_mem_blocks_free_data_instr(ext, &llext_data_heap, ptr);
#endif
}

static inline void llext_free_instr(struct llext *ext, void *ptr)
{
	if (!llext_heap_is_inited()) {
		return;
	}

#ifdef CONFIG_LLEXT_HEAP_K_HEAP
	k_heap_free(&llext_instr_heap, ptr);
#else
	llext_sys_mem_blocks_free_data_instr(ext, &llext_instr_heap, ptr);
#endif
}

static inline void llext_heap_reset(struct llext *ext)
{
#ifdef CONFIG_LLEXT_HEAP_SYS_MEM_BLOCKS
	ext->mem_alloc_map.idx = 0;
#endif
}

/*
 * ELF parsing (llext_load.c)
 */

int do_llext_load(struct llext_loader *ldr, struct llext *ext,
		  const struct llext_load_param *ldr_parm);

/*
 * Relocation (llext_link.c)
 */

int llext_link(struct llext_loader *ldr, struct llext *ext,
	       const struct llext_load_param *ldr_parm);
ssize_t llext_file_offset(struct llext_loader *ldr, uintptr_t offset);
void llext_dependency_remove_all(struct llext *ext);

#endif /* ZEPHYR_SUBSYS_LLEXT_PRIV_H_ */
