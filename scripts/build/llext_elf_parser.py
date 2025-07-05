#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

"""Functions to help read and rewrite ELF files.

Given a bytearray from an ELF file containing one or more entries
(ELF header, section header table, symbol table), edit fields inline.
"""

from elftools.elf.elffile import ELFFile

def get_field_size(struct, name):
    for field in struct.subcons:
        if field.name == name:
            return field.sizeof()

    raise ValueError(f"Unknown field name: {name}")


def get_field_offset(struct, name):
    offset = 0

    for field in struct.subcons:
        if field.name == name:
            break;
        offset += field.sizeof()

    if offset < struct.sizeof():
        return offset

    raise ValueError(f"Unknown field name: {name}")


# We write bytearray to the file afterwards
def write_field_in_struct_bytearr(elf, bytearr, name, num, ent_idx = 0):
    if name[0] == 'e':
        header = elf.structs.Elf_Ehdr
    elif name[0:2] == 'sh':
        header = elf.structs.Elf_Shdr
    elif name[0:2] == 'st':
        header = elf.structs.Elf_Sym
    else:
        raise ValueError(f"Unable to identify struct for {name}")

    size = get_field_size(header, name)

    # ent_idx is 0 for ELF header fields
    offset = (ent_idx * elf.header['e_shentsize']) + get_field_offset(header, name)

    field = None
    if elf.little_endian:
        field = num.to_bytes(size, 'little')
    else:
        field = num.to_bytes(size, 'big')

    bytearr[offset:offset+size] = field