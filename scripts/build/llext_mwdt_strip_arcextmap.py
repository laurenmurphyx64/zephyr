#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

"""Remove unused names from the section header string table. 

The MWDT strip utility does not remove the names of stripped sections from the
section header string table. This script is a workaround to remove unused
names from the section header string table, shrinking the file size. This is
necessary for boards where MWDT CCAC emits debugging sections like .arcextmap.*,
as even the smallest extension (<1k) will have a >16KB section header string table.

"""

import argparse
import logging
import os
import sys
from elftools.elf.elffile import ELFFile

logger = logging.getLogger("strip")
LOGGING_FORMAT = "[%(levelname)s][%(name)s] %(message)s"


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument("file", help="Object file")
    parser.add_argument("--debug", action="store_true",
                        help="Print extra debugging information")

    return parser.parse_args()


def get_field_size(elf_struct, field_name):
    for subcon in elf_struct.subcons:
        if subcon.name == field_name:
            return subcon.sizeof()
    raise ValueError(f"Unknown field name: {field_name}")


# We write bytearray to the file afterwards
# ARC cores are typically little-endian, but some are configurable
def write_field_to_elf_bytearr(elf, bytearr, pos, field_name, num):
    try:
        field_size = get_field_size(elf.structs.Elf_Shdr, field_name)
    except ValueError:
        field_size = get_field_size(elf.structs.Elf_Ehdr, field_name)

    if elf.little_endian:
        bytearr[pos:pos+field_size] = num.to_bytes(field_size, 'little')
    else:
        bytearr[pos:pos+field_size] = num.to_bytes(field_size, 'big')


def read_field_from_elf_file(elf, file, pos, field_name):
    try:
        field_size = get_field_size(elf.structs.Elf_Shdr, field_name)
    except ValueError:
        field_size = get_field_size(elf.structs.Elf_Ehdr, field_name)

    file.seek(pos)
    data = file.read(field_size)
    field = int.from_bytes(data, 'little' if elf.little_endian else 'big')

    return field


def read_cstring(f):
    chars = []
    while True:
        b = f.read(1)
        if not b or b == b'\x00':
            break
        chars.append(b)
    return b''.join(chars).decode('utf-8')


def strip_arcextmap(file):
    with open(file, 'r+b') as f:
        elf = ELFFile(f)

        # Read in section header table
        f.seek(elf.header['e_shoff'])
        sht = f.read(elf.header['e_shentsize'] * elf.header['e_shnum'])

        # Delete .arcextmap.* sections
        # i.e., move back sections after .arcextmap.* sections
        arcextmap_start = 0
        arcextmap_start_index = 0
        arcextmap_end = 0
        arcextmap_end_index = 0
        index = 0
        for section in elf.iter_sections():
            if section.name.startswith('.arcextmap'):
                if arcextmap_start == 0:
                    arcextmap_start_index = index
                    arcextmap_start = section['sh_offset']
                    logger.debug(f"Found first .arcextmap.* section at 0x{arcextmap_start:x}")
            if section.name == '.arcextmap':
                arcextmap_end_index = index
                arcextmap_end = section['sh_offset'] + section['sh_size']
                logger.debug(f"Found last .arcextmap section ending at 0x{arcextmap_end:x}")
            index += 1

        arcextmap_count = arcextmap_end_index - arcextmap_start_index + 1
        arcextmap_size = arcextmap_end - arcextmap_start

        last_section =  elf.get_section(index - 1)
        after_arcextmap_end = last_section['sh_offset'] + last_section['sh_size']
        after_arcextmap_size = after_arcextmap_end - arcextmap_end

        f.seek(arcextmap_end)
        after_arcextmap = f.read(after_arcextmap_size)

        f.seek(arcextmap_start)
        f.write(after_arcextmap)

        # Delete .arcextmap.* sections from section header table
        arcextmap_start_header_pos = arcextmap_start_index * elf.header['e_shentsize']
        arcextmap_end_header_pos = arcextmap_end_index * elf.header['e_shentsize']
        del sht[arcextmap_start_header_pos:arcextmap_end_header_pos + elf.header['e_shentsize']]

        # Correct remaining section headers' sh_offset fields
        for i, section in elf.iter_sections():
            if i > arcextmap_end_index:
                sh_offset_pos = (i * elf.header['e_shentsize']) + (16 if elf.elfclass == 32 else 24)
                sh_offset = section['sh_offset'] - arcextmap_size
                write_field_to_elf_bytearr(elf, sht, sh_offset_pos, "sh_offset", sh_offset)

        # List surviving sections
        sections = list(enumerate(elf.iter_sections()))
        sections = [s for s in sections if not s.name.startswith('.arcextmap')]

        # Rebuild .shstrtab
        rebuilt_shstrtab = bytearray()
        rebuilt_section_header_name_offsets = []

        offset = 0
        for section in elf.iter_sections():
            if not section.name.startswith('.arcextmap'):
                rebuilt_shstrtab += section.name.encode('utf-8') + b'\x00'
                rebuilt_section_header_name_offsets.append(offset)
                offset += len(section.name) + 1

        logger.debug(f"Rebuilt .shstrtab len: {len(rebuilt_shstrtab)}")
        logger.debug(f"Rebuilt .shstrtab (first 100 bytes): {rebuilt_shstrtab[:100]}")

        # Correct section header sh_name
        for i, section in sections:
            sh_name_offset = i * elf.header['e_shentsize'] # First field
            shstrtab_offset = rebuilt_section_header_name_offsets[i]
            write_field_to_elf_bytearr(elf, sht, sh_name_offset, "sh_name", shstrtab_offset)
            logger.debug(f"{section.name} sh_name at 0x{sh_name_offset:x} = 0x{shstrtab_offset:x}")

        # Correct .shstrtab sh_size
        shstrtab_hdr_idx = elf.header['e_shstrndx'] - arcextmap_count
        sh_size_offset = 20 if elf.elfclass == 32 else 32
        pos = (shstrtab_hdr_idx * elf.header['e_shentsize']) + sh_size_offset

        write_field_to_elf_bytearr(elf, sht, pos,)

        # Overwrite .shstrtab section with the rebuilt .shstrtab

        # Write rebuilt section header table after .shstrtab

        # Truncate the file

        # Correct e_shoff in the ELF header

        # Correct e_shnum in the ELF header

        # Correct e_shstrndx in the ELF header
        

""""
        # Copy section header table
        shoff = elf.header['e_shoff']
        shentsize = elf.header['e_shentsize']

        f.seek(shoff)
        sht = bytearray(f.read(shentsize * elf.header['e_shnum']))

        # Rebuild the section header string table
        shstrtab = elf.get_section_by_name('.shstrtab')

        rebuilt_shstrtab = bytearray()
        rebuilt_section_header_name_offsets = []

        offset = 0
        for section in elf.iter_sections():
            rebuilt_shstrtab += section.name.encode('utf-8') + b'\x00'
            rebuilt_section_header_name_offsets.append(offset)
            offset += len(section.name) + 1

        logger.debug(f"Rebuilt .shstrtab len: {len(rebuilt_shstrtab)}")
        logger.debug(f"Rebuilt .shstrtab (first 100 bytes): {rebuilt_shstrtab[:100]}")

        # Correct section header sh_name offsets
        for i, section in enumerate(elf.iter_sections()):
            sh_name_offset = i * elf.header['e_shentsize']
            shstr_offset = rebuilt_section_header_name_offsets[i]
            write_field_to_elf_bytearr(elf, sht, sh_name_offset, "sh_name", shstr_offset)
            logger.debug(f"{section.name} sh_name at 0x{sh_name_offset:x} = 0x{shstr_offset:x}")

        # Correct the .shstrtab's sh_size in the section header table
        sh_size_offset = 20 if elf.elfclass == 32 else 32
        shstrtab_sh_size_offset = (elf.header['e_shstrndx'] * shentsize) + sh_size_offset
        write_field_to_elf_bytearr(elf, sht, shstrtab_sh_size_offset, "sh_size", len(rebuilt_shstrtab))
        logger.debug(f".shstrtab sh_size at 0x{shstrtab_sh_size_offset:x} = 0x{len(rebuilt_shstrtab):x}")

        # Overwrite .shstrtab with the rebuilt string table
        f.seek(shstrtab['sh_offset'])
        f.write(rebuilt_shstrtab)

        # Update e_shoff in the ELF header as appearing after .shstrtab
        e_shoff = f.tell()
        e_shoff_bytearr = bytearray()
        write_field_to_elf_bytearr(elf, e_shoff_bytearr, 0, "e_shoff", e_shoff)

        e_shoff_offset = 32 if elf.elfclass == 32 else 40
        f.seek(e_shoff_offset)
        f.write(e_shoff_bytearr)

        logger.debug(f"e_shoff at 0x{e_shoff_offset:x} = 0x{e_shoff:x}")

        # Write modified section header table after .shstrtab
        f.seek(e_shoff)
        f.write(sht)

        # Truncate the file
        f.truncate()
"""


def main():
    args = parse_args()

    logging.basicConfig(format=LOGGING_FORMAT)

    if args.debug:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.WARNING)

    if not os.path.isfile(args.file):
        logger.error(f"Cannot find file {args.file}, exiting...")
        sys.exit(1)

    with open(args.file, 'rb') as f:
        try:
            ELFFile(f)
        except Exception:
            logger.error(f"File {args.file} is not a valid ELF file, exiting...")
            sys.exit(1)

    logger.debug(f"File to strip .arcextmap.*: {args.file}")

    try:
        strip_arcextmap(args.file)
    except Exception as e:
        logger.error(f"An error occurred while processing the file: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
