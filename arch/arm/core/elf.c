/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/llext/elf.h>
#include <zephyr/llext/llext.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(elf, CONFIG_LLEXT_LOG_LEVEL);

uint32_t mask_n_from_i(int n, int i) {
	return (uint32_t)((1 << n) - 1) << i;
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
	case R_ARM_THM_JUMP24:
		/* Referencing:
		 * - https://github.com/angr/cle/blob/master/cle/backends/elf/relocation/arm.py
		 * - https://developer.arm.com/documentation/ddi0308/d/Thumb-Instructions/Alphabetical-list-of-Thumb-instructions/BL--BLX--immediate-
		 * offset = (((S + A) | T) - P)
		 * opaddr = P */
		uint32_t sym_loc = opval - *((uintptr_t *)opaddr); /* S */
		uint32_t is_thumb = (opaddr % 2 == 1) && ELF_ST_TYPE(sym->st_info) == STT_SECTION; /* T* */
		uint32_t addend = 0; /* A */
		
		uint32_t offset = 0;

		uint32_t inst = *((uintptr_t *)opaddr);

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

		sign_bit = ((inst & mask_n_from_i(1, 26)) & 1) ? 1 : 0; /* sign_bit = inst[26] */

		j1 = (((inst & mask_n_from_i(1, 13)) ? 1 : 0) & 1) ^ (!sign_bit);  /* J1 = inst[13] ^ !sign */
		j2 = (((inst & mask_n_from_i(1, 11)) ? 1 : 0) & 1) ^ (!sign_bit);  /* J1 = inst[13] ^ !sign */

		addend |= j1 << 23; /* A[23] = J1 */
		addend |= j2 << 22; /* A[22] = J2 */

		addend &= 0x7FFFFF;
		if (sign_bit)
			addend |= 0xFF800000;

		/* Compute offset and mask to 32 bits */
		offset = (((sym_loc + addend) | is_thumb) - opaddr) & 0xFFFFFFFF; /* < TODO: WHY??? */

		/* Reconstruct instruction, clearing out addend data to be replaced with offset
		 *                    offset     1 2  offset
         *            11110S [21:12]  11J?J  [11:1]     (if ? is 1, BL; if ? is 0, BLX)
         * inst &= ~0b00000111111111110010111111111111
         *         |       |       |       |       |
         *        32      24      16       8       0
		 */
		inst &= ~0x07ff3000;

		sign_bit = ((offset & mask_n_from_i(1, 24)) ? 1 : 0) & 1;
		j1 = (((offset & mask_n_from_i(1,23)) ? 1 : 0) & 1) ^ (!sign_bit);
		j2 = (((offset & mask_n_from_i(1,22)) ? 1 : 0) & 1) ^ (!sign_bit);

		/* instr = 11110:S:imm10:1:1:J1:1:J2:imm11:0 */
		inst |= sign_bit << 26;
		inst |= j1 << 13;
		inst |= j2 << 11;
		inst |= (offset & mask_n_from_i(11, 1)) >> 1; /* The LSB of offset isn't important??? */
		inst |= ((offset & mask_n_from_i(10, 12)) >> 12) << 16;

		/* Put it back into <little endian short> <little endian short> format */
		uint32_t raw[4] = {
			(inst & 0x00FF0000) >> 16, (inst & 0xFF000000) >> 24,
			(inst & 0x00FF), (inst & 0xFF00) >> 8};
		
		/* Flip for little Endian result (???) */
        inst = (raw[3] << 24) | (raw[2] << 16) | (raw[1] << 8) | raw[0];

		*((uint32_t *)opaddr) = inst;
		break;
	default:
		LOG_DBG("Unsupported ARM elf relocation type %d at address %lx",
			reloc_type, opaddr);
		break;
	}
}