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

inline uint32_t bit(uint32_t val, int32_t pos) {
	return (val >> pos) & 1;
}

inline uint32_t bits(uint32_t val, uint32_t hi, uint32_t lo) {
  return (val >> lo) & ((1L << (hi - lo + 1)) - 1);
}

inline uint32_t sign_extend(uint32_t val, uint32_t size) {
  return (int32_t)(val << (31 - size)) >> (31 - size);
}

inline uint32_t align_to(uint32_t val, uint32_t align) {
  if (align == 0)
    return val;
//   assert(has_single_bit(align));
  return (val + align - 1) & ~(align - 1);
}

bool is_jump_reachable(int32_t val) {
  return sign_extend(val, 24) == val;
}

int32_t get_addend(uint8_t *loc) {
	uint32_t S = bit(*(uint16_t *)loc, 10);
    uint32_t J1 = bit(*(uint16_t *)(loc + 2), 13);
    uint32_t J2 = bit(*(uint16_t *)(loc + 2), 11);
    uint32_t I1 = !(J1 ^ S);
    uint32_t I2 = !(J2 ^ S);
    uint32_t imm10 = bits(*(uint16_t *)loc, 9, 0);
    uint32_t imm11 = bits(*(uint16_t *)(loc + 2), 10, 0);
    uint32_t val = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
    return sign_extend(val, 24);
}

uint32_t get_arm_thunk_addr() {
	return 0; // TODO
}

void write_thm_b_imm(uint8_t *loc, uint32_t val) {
  // https://developer.arm.com/documentation/ddi0406/cb/Application-Level-Architecture/Instruction-Details/Alphabetical-list-of-instructions/BL--BLX--immediate-
  uint32_t sign = bit(val, 24);
  uint32_t I1 = bit(val, 23);
  uint32_t I2 = bit(val, 22);
  uint32_t J1 = !I1 ^ sign;
  uint32_t J2 = !I2 ^ sign;
  uint32_t imm10 = bits(val, 21, 12);
  uint32_t imm11 = bits(val, 11, 1);

  uint16_t *buf = (uint16_t *)loc;
  buf[0] = (buf[0] & 0b1111100000000000) | (sign << 10) | imm10;
  buf[1] = (buf[1] & 0b1101000000000000) | (J1 << 13) | (J2 << 11) | imm11;
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
		// if (sym.is_remaining_undef_weak()) {
		// 	// On ARM, calling an weak undefined symbol jumps to the
		// 	// next instruction.
		// 	*(ul32 *)loc = 0x8000'f3af; // NOP.W
		// 	break;
		// }

		/* Referencing:
		 * https://developer.arm.com/documentation/ddi0308/d/Thumb-Instructions/Alphabetical-list-of-Thumb-instructions/BL--BLX--immediate-
		 * http://hermes.wings.cs.wisc.edu/files/Thumb-2SupplementReferenceManual.pdf pg. 37, 72, 134
		 * https://github.com/rui314/mold/blob/main/elf/arch-arm32.cc#L64,L294
		 * 
		 * offset = (((S + A) | T) - P)
		 * opaddr = P 
		 */
		uint32_t sym_loc = opval - *((uintptr_t *)opaddr); /* S */
		bool is_thumb = sym_loc & 1; /* T* */
		int32_t addend = get_addend((uint8_t *)opaddr); /* A */
		printk("sym_loc %x\n", sym_loc);
		printk("opaddr %x\n", opaddr);

		/* offset = S + A - P*/
		// int32_t offset = sym_loc + addend - opaddr;
		int32_t offset = sym_loc + addend - opaddr;

		if (is_jump_reachable(offset)) {
			if (is_thumb) {
				write_thm_b_imm(opaddr, offset);
				*(uint16_t *)(opaddr + 2) |= 0x1000;  // rewrite to BL
			} else {
				write_thm_b_imm(opaddr, align_to(offset, 4));
				*(uint16_t *)(opaddr + 2) &= ~0x1000; // rewrite to BLX
			}
		} else {
		 	write_thm_b_imm(opaddr, align_to(get_arm_thunk_addr() + addend - opaddr, 4));
		 	*(uint16_t *)(opaddr + 2) &= ~0x1000;  // rewrite to BLX
		}

		break;
	default:
		LOG_DBG("Unsupported ARM elf relocation type %d at address %lx",
			reloc_type, opaddr);
		break;
	}
}