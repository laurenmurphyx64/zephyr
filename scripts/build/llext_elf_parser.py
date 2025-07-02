#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

"""Functions to help read and rewrite ELF files.
"""

from elftools.elf.elffile import ELFFile

def get_field_size(header, name):
    for field in header.subcons:
        if field.name == name:
            return field.sizeof()

    raise ValueError(f"Unknown field name: {name}")


def get_field_offset(header, name):
    offset = 0

    for field in header.subcons:
        if field.name == name:
            break;
        offset += field.sizeof()

    if offset < header.sizeof():
        return offset

    raise ValueError(f"Unknown field name: {name}")


# We write bytearray to the file afterwards
def write_field_to_header_bytearr(elf, bytearr, name, num, sh_ent_idx = 0):
    if name[0] == 'e':
        header = elf.structs.Elf_Ehdr
    else:
        header = elf.structs.Elf_Shdr

    size = get_field_size(header, name)

    # sh_ent_idx is 0 for ELF header fields
    offset = (sh_ent_idx * elf.header['e_shentsize']) + get_field_offset(header, name)

    field = None
    if elf.little_endian:
        field = num.to_bytes(size, 'little')
    else:
        field = num.to_bytes(size, 'big')

    bytearr[offset:offset+size] = field