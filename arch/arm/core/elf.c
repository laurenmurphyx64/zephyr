/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/elf.h>
#include <zephyr/llext/llext.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(elf, CONFIG_LLEXT_LOG_LEVEL);

uint32_t mask_n_from_i(int n, int i) {
	uint32_t mask  = ((1 << n) - 1) << i;
	return mask;
}

/**
 * @brief Architecture specific function for relocating partially linked (static) elf
 *
 * Elf files contain a series of relocations described in a section. These relocation
 * instructions are architecture specific and each architecture supporting extensions
 * must implement this.
 *
 * The relocation codes for arm are well documented
 * https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst#relocation
 */
void arch_elf_relocate(elf_rela_t *rel, elf_sym_t *sym, uintptr_t opaddr, uintptr_t opval)
{
	elf_word reloc_type = ELF32_R_TYPE(rel->r_info);

	switch (reloc_type) {
	case R_ARM_ABS32:
		/* Update the absolute address of a load/store instruction */
		*((uint32_t *)opaddr) = (uint32_t)opval;
		break;
	case R_ARM_THM_CALL:
		/* Referencing:
		 * - https://github.com/angr/cle/blob/master/cle/backends/elf/relocation/arm.py
		 * - https://developer.arm.com/documentation/ddi0308/d/Thumb-Instructions/Alphabetical-list-of-Thumb-instructions/BL--BLX--immediate-
		 * - http://hermes.wings.cs.wisc.edu/files/Thumb-2SupplementReferenceManual.pdf pg. 37, 72 
		 *
		 * offset = (((S + A) | T) - P)
		 * opaddr = P 
		 */
		uint32_t sym_loc = opval - *((uintptr_t *)opaddr); /* S */
		bool is_thumb = ((sym_loc % 2 == 1) && (ELF_ST_TYPE(sym->st_info) == STT_FUNC)) ? 1 : 0; /* T* */
		int32_t addend = 0; /* = rel->r_addend; A */
		int32_t offset = 0;

		printk("**** sym_loc: %p\n", sym_loc);
		printk("**** opaddr: %p\n", opaddr);
		printk("**** isThumb: %d\n", is_thumb);

		/* Bytes are in order b3 b4 b1 b2 in memory, instruction is ordered
		 * b4:b3:b2:b1 with b4 being MSB, b4:b3 being hw1 and b2:b1 being hw2  */
		uint32_t inst = ((uint8_t *)opaddr)[1] << 24 | ((uint8_t *)opaddr)[0] << 16 | \
			((uint8_t *)opaddr)[3] << 8 | ((uint8_t *)opaddr)[2];

		printk("**** inst: %x\n", inst);

		uint32_t sign_bit = 0;
		uint32_t j1;
		uint32_t j2;

		/* Decode compiler-provided addend from instruction immediate
		 * instr = 11110:S:imm10:1:1:J1:1:J2:imm11
		 * A = J2:J1:imm10:imm11:0
		 * TODO: Where and why is A this
		 */
		addend |= (inst & mask_n_from_i(11, 0)) << 1; /* A[11:1] = inst[10:0]*/
		addend |= ((inst & mask_n_from_i(10, 16)) >> 16) << 12; /* A[21:12] = inst[25:16]*/

		sign_bit = ((inst & mask_n_from_i(1, 26)) & 1); /* sign_bit = inst[26] */

		j1 = ((inst & mask_n_from_i(1, 13)) & 1) ^ (!sign_bit);  /* J1 = inst[13] ^ !sign */
		j2 = ((inst & mask_n_from_i(1, 11)) & 1) ^ (!sign_bit);  /* J1 = inst[13] ^ !sign */

		addend |= j1 << 23; /* A[23] = J1 */
		addend |= j2 << 22; /* A[22] = J2 */

		addend &= 0x7FFFFF;
		if (sign_bit)
			addend |= 0xFF800000; /* SignExtend */

		printk("**** sign_bit: %x\n", sign_bit);

		printk("**** addend: %x\n", addend);

		addend = rel->r_addend;

		printk("**** addend: %x\n", addend);

		/* Compute offset */
		// offset = (((sym_loc + addend) | is_thumb) - opaddr); /* < TODO: WHY??? */
		offset = sym_loc - opaddr;

		printk("**** offset: %x\n", offset);

		/* Rebuild the instruction, first clearing out any previously set offset bits
		 *                    offset     1 2  offset
         *            11110S [21:12]  11J?J  [11:1]     (if ? is 1, BL; if ? is 0, BLX)
         * inst &= ~0b00000111111111110010111111111111
         *         |       |       |       |       |
         *        32      24      16       8       0
		 */
		// inst &= ~0x07ff3000;
		inst &= 0xF800D000;

		sign_bit = (offset & mask_n_from_i(1, 24)) & 1;
		j1 = (offset & mask_n_from_i(1,23) & 1) ^ (!sign_bit);
		j2 = (offset & mask_n_from_i(1,22) & 1) ^ (!sign_bit);

		/* instr = 11110:S:imm10:1:1:J1:1:J2:imm11:0 */
		inst |= sign_bit << 26;
		inst |= j1 << 13;
		inst |= j2 << 11;
		inst |= (offset & mask_n_from_i(11, 1)) >> 1; /* The LSB of offset isn't important??? */
		inst |= ((offset & mask_n_from_i(10, 12)) >> 12) << 16;

		printk("resulting instruction: %x\n", inst);

		/* b3 b4 b1 b2 */
		((uint8_t *)opaddr)[0] = (inst & 0x00FF0000) >> 16;
		((uint8_t *)opaddr)[1] = (inst & 0xFF000000) >> 24;
		((uint8_t *)opaddr)[2] = (inst & 0x00FF);
		((uint8_t *)opaddr)[3] = (inst & 0xFF00) >> 8;

		printk("memory: %x\n", *(uint32_t *)opaddr);
		break;
	default:
		LOG_DBG("Unsupported ARM elf relocation type %d at address %lx",
			reloc_type, opaddr);
		break;
	}
}