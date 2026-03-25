/*
 * Copyright (c) 2023 Intel Corporation
 * Copyright (c) 2024 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/llext/loader.h>
#include <zephyr/llext/llext.h>
#include <zephyr/kernel.h>
#include <zephyr/cache.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(llext, CONFIG_LLEXT_LOG_LEVEL);

#include <string.h>

#include "llext_priv.h"
#include "llext_mem.h"

#ifdef CONFIG_LLEXT_HEAP_DYNAMIC
bool llext_heap_inited;
#endif

/*
 * Initialize the memory partition associated with the specified memory region
 */
static void llext_init_mem_part(struct llext *ext, enum llext_mem mem_idx,
			uintptr_t start, size_t len)
{
#ifdef CONFIG_USERSPACE
	if (mem_idx < LLEXT_MEM_PARTITIONS) {
		ext->mem_parts[mem_idx].start = start;
		ext->mem_parts[mem_idx].size = len;

		switch (mem_idx) {
		case LLEXT_MEM_TEXT:
			ext->mem_parts[mem_idx].attr = K_MEM_PARTITION_P_RX_U_RX;
			break;
		case LLEXT_MEM_DATA:
		case LLEXT_MEM_BSS:
			ext->mem_parts[mem_idx].attr = K_MEM_PARTITION_P_RW_U_RW;
			break;
		case LLEXT_MEM_RODATA:
			ext->mem_parts[mem_idx].attr = K_MEM_PARTITION_P_RO_U_RO;
			break;
		default:
			break;
		}
	}
#endif

	LOG_DBG("region %d: start %#zx, size %zd", mem_idx, (size_t)start, len);
}

#ifdef CONFIG_LLEXT_INSTR_WORD_SIZED_ALIGNED_ACCESS
static inline void llext_copy_byte_word_aligned(unsigned char *dst, const unsigned char *src)
{
	/* Mask off the lower bits to get an aligned pointer */
	const uintptr_t mask = sizeof(uintptr_t) - 1;
	uintptr_t d_aligned = (uintptr_t)dst & ~mask;
	uintptr_t s_aligned = (uintptr_t)src & ~mask;
	unsigned int d_offset = (uintptr_t)dst & mask;
	unsigned int s_offset = (uintptr_t)src & mask;

	/* Read aligned words*/
	uintptr_t d_word = *(uintptr_t *)d_aligned;
	uintptr_t s_word = *(uintptr_t *)s_aligned;

	/* Extract byte from source and insert into destination */
	unsigned char byte_val = (s_word >> (s_offset * 8)) & 0xFF;
	uintptr_t byte_mask = ~((uintptr_t)0xFF << (d_offset * 8));

	d_word = (d_word & byte_mask) | ((uintptr_t)byte_val << (d_offset * 8));

	*(uintptr_t *)d_aligned = d_word;
}

static inline void llext_set_byte_word_aligned(unsigned char *dst, unsigned char value)
{
	const uintptr_t mask = sizeof(uintptr_t) - 1;
	uintptr_t d_aligned = (uintptr_t)dst & ~mask;
	unsigned int d_offset = (uintptr_t)dst & mask;

	uintptr_t d_word = *(uintptr_t *)d_aligned;

	/* Clear destination byte and insert new value */
	uintptr_t byte_mask = ~((uintptr_t)0xFF << (d_offset * 8));

	d_word = (d_word & byte_mask) | ((uintptr_t)value << (d_offset * 8));

	*(uintptr_t *)d_aligned = d_word;
}
#endif /* CONFIG_LLEXT_INSTR_WORD_SIZED_ALIGNED_ACCESS */

/*
 * Memset for instruction region on heap. Some forms of instruction memory,
 * e.g. Xtensa IRAM, do not support narrow load / store, necessitating
 * a memset that only performs word-aligned and word-sized stores.
 */
void *llext_instr_memset(void *buf, int c, size_t n)
{
#ifndef CONFIG_LLEXT_INSTR_WORD_SIZED_ALIGNED_ACCESS
	LOG_ERR("regular memset");
	return memset(buf, c, n);
#else
	/* do byte-sized initialization until word-aligned or finished */
	unsigned char *d_byte = (unsigned char *)buf;
	unsigned char c_byte = (unsigned char)c;

#if !defined(CONFIG_MINIMAL_LIBC_OPTIMIZE_STRING_FOR_SIZE)
	while (((uintptr_t)d_byte) & (sizeof(uintptr_t) - 1)) {
		if (n == 0) {
			return buf;
		}
		*(d_byte++) = c_byte;
		llext_set_byte_word_aligned(d_byte++, c_byte);
		n--;
	}

	/* do word-sized initialization as long as possible */

	uintptr_t *d_word = (uintptr_t *)d_byte;
	uintptr_t c_word = (uintptr_t)c_byte;

	c_word |= c_word << 8;
	c_word |= c_word << 16;
#if Z_MEM_WORD_T_WIDTH > 32
	c_word |= c_word << 32;
#endif

	while (n >= sizeof(uintptr_t)) {
		*(d_word++) = c_word;
		n -= sizeof(uintptr_t);
	}

	/* do byte-sized initialization until finished */

	d_byte = (unsigned char *)d_word;
#endif

	while (n > 0) {
#if !defined(CONFIG_MINIMAL_LIBC_OPTIMIZE_STRING_FOR_SIZE)
		llext_set_byte_word_aligned(d_byte++, c_byte);
#else
		*(d_byte++) = c_byte;
#endif
		n--;
	}

	return buf;
#endif
}

/*
 * Memcpy for instruction region on heap. Some forms of instruction memory,
 * e.g. Xtensa IRAM, do not support narrow load / store, necessitating
 * a memcpy that only performs word-aligned and word-sized loads / stores.
 */
void *llext_instr_memcpy(void *ZRESTRICT d, const void *ZRESTRICT s, size_t n)
{
#ifndef CONFIG_LLEXT_INSTR_WORD_SIZED_ALIGNED_ACCESS
	LOG_ERR("regular memcpy");
	return memcpy(d, s, n);
#else
	/* Attempt word-sized copying only if buffers have identical alignment */
	unsigned char *d_byte = (unsigned char *)d;
	const unsigned char *s_byte = (const unsigned char *)s;

#if !defined(CONFIG_MINIMAL_LIBC_OPTIMIZE_STRING_FOR_SIZE)
	const uintptr_t mask = sizeof(uintptr_t) - 1;

	if ((((uintptr_t)d ^ (uintptr_t)s_byte) & mask) == 0) {
		/* Deal with unaligned first bytes */
		while (((uintptr_t)d_byte) & mask) {
			if (n == 0) {
				return d;
			}
			llext_copy_byte_word_aligned(d_byte++, s_byte++);
			n--;
		}

		/* Do word-sized copying as long as possible */
		uintptr_t *d_word = (uintptr_t *)d_byte;
		const uintptr_t *s_word = (const uintptr_t *)s_byte;

		while (n >= sizeof(uintptr_t)) {
			*(d_word++) = *(s_word++);
			n -= sizeof(uintptr_t);
		}

		d_byte = (unsigned char *)d_word;
		s_byte = (unsigned char *)s_word;
	}
#endif

	/* Do byte-sized copying until finished */
	while (n > 0) {
#if !defined(CONFIG_MINIMAL_LIBC_OPTIMIZE_STRING_FOR_SIZE)
		llext_copy_byte_word_aligned(d_byte++, s_byte++);
#else
		*(d_byte++) = *(s_byte++);
#endif
		n--;
	}

	return d;
#endif /* CONFIG_LLEXT_INSTR_WORD_SIZED_ALIGNED_ACCESS */
}

static int llext_copy_region(struct llext_loader *ldr, struct llext *ext,
			      enum llext_mem mem_idx, const struct llext_load_param *ldr_parm)
{
	int ret;
	elf_shdr_t *region = ldr->sects + mem_idx;
	uintptr_t region_alloc = region->sh_size;
	uintptr_t region_align = region->sh_addralign;

	if (!region_alloc) {
		return 0;
	}
	ext->mem_size[mem_idx] = region_alloc;

	/*
	 * Calculate the minimum region size and alignment that can satisfy
	 * MMU/MPU requirements. This only applies to regions that contain
	 * program-accessible data (not to string tables, for example).
	 */
	if (region->sh_flags & SHF_ALLOC) {
		if (IS_ENABLED(CONFIG_MMU)) {
			/* MMU targets map memory in page-sized chunks. Round
			 * the region to multiples of those.
			 */
			region_alloc = ROUND_UP(region_alloc, LLEXT_PAGE_SIZE);
			region_align = MAX(region_align, LLEXT_PAGE_SIZE);
		} else if (IS_ENABLED(CONFIG_USERSPACE)) {
			if (IS_ENABLED(CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT)) {
				/* Some MPU architectures (ARMv7-M, older ARC) require regions
				 * to be sized and aligned to the same power of two.
				 */
				uintptr_t block_sz =
					MAX(MAX(region_alloc, region_align), LLEXT_PAGE_SIZE);

				block_sz = 1 << LOG2CEIL(block_sz); /* align to next power of two */
				region_alloc = block_sz;
				region_align = block_sz;
			} else if (IS_ENABLED(CONFIG_ARM_MPU) || IS_ENABLED(CONFIG_ARC_MPU)) {
				/* ARMv8-M and newer ARC MPUs use 32-byte alignment. */
				region_alloc = ROUND_UP(region_alloc, LLEXT_PAGE_SIZE);
				region_align = MAX(region_align, LLEXT_PAGE_SIZE);
			}
		}
	}

	if (ldr->storage == LLEXT_STORAGE_WRITABLE ||           /* writable storage         */
	    (ldr->storage == LLEXT_STORAGE_PERSISTENT &&        /* || persistent storage    */
	     !(region->sh_flags & SHF_WRITE) &&                 /*    && read-only region   */
	     !(region->sh_flags & SHF_LLEXT_HAS_RELOCS))) {     /*    && no relocs to apply */
		/*
		 * Try to reuse data areas from the ELF buffer, if possible.
		 * If any of the following tests fail, a normal allocation
		 * will be attempted.
		 */
		if (region->sh_type != SHT_NOBITS) {
			/* Region has data in the file, check if peek() is supported */
			ext->mem[mem_idx] = llext_peek(ldr, region->sh_offset);
			if (ext->mem[mem_idx]) {
				if ((IS_ALIGNED(ext->mem[mem_idx], region_align) ||
				     ldr_parm->pre_located) &&
				    ((mem_idx != LLEXT_MEM_TEXT) ||
				     INSTR_FETCHABLE(ext->mem[mem_idx], region_alloc))) {
					/* Map this region directly to the ELF buffer */
					llext_init_mem_part(ext, mem_idx,
							    (uintptr_t)ext->mem[mem_idx],
							    region_alloc);
					ext->mem_on_heap[mem_idx] = false;
					return 0;
				}

				if ((mem_idx == LLEXT_MEM_TEXT) &&
				    !INSTR_FETCHABLE(ext->mem[mem_idx], region_alloc)) {
					LOG_WRN("Cannot reuse ELF buffer for region %d, not "
						"instruction memory: %p-%p",
						mem_idx, ext->mem[mem_idx],
						(void *)((uintptr_t)(ext->mem[mem_idx]) +
							 region->sh_size));
				}
				if (!IS_ALIGNED(ext->mem[mem_idx], region_align)) {
					LOG_WRN("Cannot peek region %d: %p not aligned to %#zx",
						mem_idx, ext->mem[mem_idx], (size_t)region_align);
				}
			}
		} else if (ldr_parm->pre_located) {
			/*
			 * In pre-located files all regions, including BSS,
			 * are placed by the user with a linker script. No
			 * additional memory allocation is needed here.
			 */
			ext->mem[mem_idx] = NULL;
			ext->mem_on_heap[mem_idx] = false;
			return 0;
		}
	}

	if (ldr_parm->pre_located) {
		/*
		 * The ELF file is supposed to be pre-located, but some
		 * regions are not accessible or not in the correct place.
		 */
		return -EFAULT;
	}

#ifdef CONFIG_LLEXT_HEAP_MEMBLK
	/* If allocating to heap, allocation must be multiple of block size */
	region_alloc = ROUND_UP(region_alloc, CONFIG_LLEXT_HEAP_MEMBLK_BLOCK_SIZE);
#endif

	/* Allocate a suitably aligned area for the region. */
	if (region->sh_flags & SHF_EXECINSTR) {
		ext->mem[mem_idx] = llext_aligned_alloc_instr(ext, region_align, region_alloc);
	} else {
		ext->mem[mem_idx] = llext_aligned_alloc_data(ext, region_align, region_alloc);
	}

	if (!ext->mem[mem_idx]) {
		LOG_ERR("Failed allocating %zd bytes %zd-aligned for region %d",
			(size_t)region_alloc, (size_t)region_align, mem_idx);
		return -ENOMEM;
	}

	ext->alloc_size += region_alloc;

	llext_init_mem_part(ext, mem_idx, (uintptr_t)ext->mem[mem_idx],
		region_alloc);

	if (region->sh_type == SHT_NOBITS) {
		memset(ext->mem[mem_idx], 0, region->sh_size);
	} else {
		uintptr_t base = (uintptr_t)ext->mem[mem_idx];
		size_t offset = region->sh_offset;
		size_t length = region->sh_size;

		if (region->sh_flags & SHF_ALLOC) {
			/* zero out any prepad bytes, not part of the data area */
			size_t prepad = region->sh_info;

			if (mem_idx == LLEXT_MEM_TEXT) {
				llext_instr_memset((void *)base, 0, prepad);
			} else {
				memset((void *)base, 0, prepad);
			}
			base += prepad;
			offset += prepad;
			length -= prepad;
		}

		/* actual data area without prepad bytes */
		ret = llext_seek(ldr, offset);
		if (ret != 0) {
			goto err;
		}

		if (mem_idx == LLEXT_MEM_TEXT) {
			ret = llext_read_instr(ldr, (void *)base, length);
		} else {
			ret = llext_read(ldr, (void *)base, length);
		}
		if (ret != 0) {
			goto err;
		}
	}

	ext->mem_on_heap[mem_idx] = true;

	return 0;

err:
	if (region->sh_flags & SHF_EXECINSTR) {
		llext_free_instr(ext, ext->mem[mem_idx]);
	} else {
		llext_free_data(ext, ext->mem[mem_idx]);
	}
	ext->mem[mem_idx] = NULL;
	return ret;
}

int llext_copy_strings(struct llext_loader *ldr, struct llext *ext,
		       const struct llext_load_param *ldr_parm)
{
	llext_heap_reset(ext);

	int ret = llext_copy_region(ldr, ext, LLEXT_MEM_SHSTRTAB, ldr_parm);

	if (!ret) {
		ret = llext_copy_region(ldr, ext, LLEXT_MEM_STRTAB, ldr_parm);
	}

	return ret;
}

int llext_copy_regions(struct llext_loader *ldr, struct llext *ext,
		       const struct llext_load_param *ldr_parm)
{
	for (enum llext_mem mem_idx = 0; mem_idx < LLEXT_MEM_COUNT; mem_idx++) {
		/* strings have already been copied */
		if (ext->mem[mem_idx]) {
			continue;
		}

		int ret = llext_copy_region(ldr, ext, mem_idx, ldr_parm);

		if (ret < 0) {
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_LLEXT_LOG_LEVEL_DBG)) {
		LOG_DBG("gdb add-symbol-file flags:");
		for (int i = 0; i < ext->sect_cnt; ++i) {
			elf_shdr_t *shdr = ext->sect_hdrs + i;
			enum llext_mem mem_idx = ldr->sect_map[i].mem_idx;
			const char *name = llext_section_name(ldr, ext, shdr);

			/* only show sections mapped to program memory */
			if (mem_idx < LLEXT_MEM_EXPORT) {
				LOG_DBG("-s %s %#zx", name,
					(size_t)ext->mem[mem_idx] + ldr->sect_map[i].offset);
			}
		}
	}

	return 0;
}

void llext_adjust_mmu_permissions(struct llext *ext)
{
#ifdef CONFIG_MMU
	void *addr;
	size_t size;
	uint32_t flags;

	for (enum llext_mem mem_idx = 0; mem_idx < LLEXT_MEM_PARTITIONS; mem_idx++) {
		addr = ext->mem[mem_idx];
		size = ROUND_UP(ext->mem_size[mem_idx], LLEXT_PAGE_SIZE);
		if (size == 0) {
			continue;
		}
		switch (mem_idx) {
		case LLEXT_MEM_TEXT:
			sys_cache_instr_invd_range(addr, size);
			flags = K_MEM_PERM_EXEC;
			break;
		case LLEXT_MEM_DATA:
		case LLEXT_MEM_BSS:
			/* memory is already K_MEM_PERM_RW by default */
			continue;
		case LLEXT_MEM_RODATA:
			flags = 0;
			break;
		default:
			continue;
		}
		sys_cache_data_flush_range(addr, size);
		k_mem_update_flags(addr, size, flags);
	}

	ext->mmu_permissions_set = true;
#endif
}

void llext_free_regions(struct llext *ext)
{
	for (int i = 0; i < LLEXT_MEM_COUNT; i++) {
#ifdef CONFIG_MMU
		if (ext->mmu_permissions_set && ext->mem_size[i] != 0 &&
		    (i == LLEXT_MEM_TEXT || i == LLEXT_MEM_RODATA)) {
			/* restore default RAM permissions of changed regions */
			k_mem_update_flags(ext->mem[i],
					   ROUND_UP(ext->mem_size[i], LLEXT_PAGE_SIZE),
					   K_MEM_PERM_RW);
		}
#endif
		if (ext->mem_on_heap[i]) {
			LOG_DBG("freeing memory region %d", i);

			if (i == LLEXT_MEM_TEXT) {
				llext_free_instr(ext, ext->mem[i]);
			} else {
				llext_free_data(ext, ext->mem[i]);
			}

			ext->mem[i] = NULL;
		}
	}

	llext_heap_reset(ext);
}

int llext_add_domain(struct llext *ext, struct k_mem_domain *domain)
{
#ifdef CONFIG_USERSPACE
	int ret = 0;

	for (int i = 0; i < LLEXT_MEM_PARTITIONS; i++) {
		if (ext->mem_size[i] == 0) {
			continue;
		}
		ret = k_mem_domain_add_partition(domain, &ext->mem_parts[i]);
		if (ret != 0) {
			LOG_ERR("Failed adding memory partition %d to domain %p",
				i, domain);
			return ret;
		}
	}

	return ret;
#else
	return -ENOSYS;
#endif
}

int llext_heap_init_harvard(void *instr_mem, size_t instr_bytes, void *data_mem, size_t data_bytes)
{
#if !defined(CONFIG_LLEXT_HEAP_DYNAMIC) || !defined(CONFIG_HARVARD)
	return -ENOSYS;
#else
	if (llext_heap_inited) {
		return -EEXIST;
	}

	k_heap_init(&llext_instr_heap, instr_mem, instr_bytes);
	k_heap_init(&llext_data_heap, data_mem, data_bytes);

	llext_heap_inited = true;
	return 0;
#endif
}

int llext_heap_init(void *mem, size_t bytes)
{
#if !defined(CONFIG_LLEXT_HEAP_DYNAMIC) || defined(CONFIG_HARVARD)
	return -ENOSYS;
#else
	if (llext_heap_inited) {
		return -EEXIST;
	}

	k_heap_init(&llext_heap, mem, bytes);

	llext_heap_inited = true;
	return 0;
#endif
}

#ifdef CONFIG_LLEXT_HEAP_DYNAMIC
static int llext_loaded(struct llext *ext, void *arg)
{
	return 1;
}
#endif

int llext_heap_uninit(void)
{
#ifdef CONFIG_LLEXT_HEAP_DYNAMIC
	if (!llext_heap_inited) {
		return -EEXIST;
	}
	if (llext_iterate(llext_loaded, NULL)) {
		return -EBUSY;
	}
	llext_heap_inited = false;
	return 0;
#else
	return -ENOSYS;
#endif
}
