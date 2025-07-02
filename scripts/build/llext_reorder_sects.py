#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

'''Reorder sections in an ELF file.

The LLEXT loader groups sections into regions. If sections are
not ordered correctly, regions may overlap, causing the loader to fail.
This script reorders sections in an ELF file to ensure regions will not overlap.
'''

import argparse
import logging
import os
import sys
import shutil
from enum import Enum

from elftools.elf.elffile import ELFFile
from elftools.elf.constants import SH_FLAGS
from llext_elf_parser import write_field_to_header_bytearr

logger = logging.getLogger('reorder')
LOGGING_FORMAT = '[%(levelname)s][%(name)s] %(message)s'


class LlextMem(Enum):
    LLEXT_MEM_TEXT = 0
    LLEXT_MEM_DATA = 1
    LLEXT_MEM_RODATA = 2
    LLEXT_MEM_BSS = 3
    LLEXT_MEM_EXPORT = 4
    LLEXT_MEM_SYMTAB = 5
    LLEXT_MEM_STRTAB = 6
    LLEXT_MEM_SHSTRTAB = 7
    LLEXT_MEM_PREINIT = 8
    LLEXT_MEM_INIT = 9
    LLEXT_MEM_FINI = 10

    LLEXT_MEM_COUNT = 11
    LLEXT_MEM_NULL = 12


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument('file', help='Object file')
    parser.add_argument('--debug', action='store_true',
                        help='Print extra debugging information')

    return parser.parse_args()


def get_region_for_section(section):
    if (section['sh_index'] == 0):
        mem_idx = LlextMem.LLEXT_MEM_NULL.value
    elif (section['sh_type'] == 'SHT_NOBITS'):
        mem_idx = LlextMem.LLEXT_MEM_BSS.value
    elif (section['sh_type'] == 'SHT_PROGBITS'):
        if (section['sh_flags'] & SH_FLAGS.SHF_EXECINSTR):
            mem_idx = LlextMem.LLEXT_MEM_TEXT.value
        elif (section['sh_flags'] & SH_FLAGS.SHF_WRITE):
            mem_idx = LlextMem.LLEXT_MEM_DATA.value
        else:
            mem_idx = LlextMem.LLEXT_MEM_RODATA.value
    elif (section['sh_type'] == 'SHT_PREINIT_ARRAY'):
        mem_idx = LlextMem.LLEXT_MEM_PREINIT.value
    elif (section['sh_type'] == 'SHT_INIT_ARRAY'):
        mem_idx = LlextMem.LLEXT_MEM_INIT.value
    elif (section['sh_type'] == 'SHT_FINI_ARRAY'):
        mem_idx = LlextMem.LLEXT_MEM_FINI.value
    else:
        mem_idx = LlextMem.LLEXT_MEM_COUNT.value

    if (section.name == '.exported_sym'):
        mem_idx = LlextMem.LLEXT_MEM_EXPORT.value

    logger.debug(f'Section {section.name} assigned to region {LlextMem(mem_idx).name}')

    return mem_idx


def reorder_sections(f, bak):
    elf = ELFFile(bak)

    elfh = bytearray()

    e_shstrndx = 0

    sht = bytearray()

    # Read in ELF header
    bak.seek(0) # elftools moves the file pointer
    elfh += bak.read(elf.header['e_ehsize'])

    # Move the file pointer forward past the ELF header
    # We will correct it inline later
    f.write(elfh)

    # Get list of sections ordered by region
    region_for_sects = {i: [] for i in range(LlextMem.LLEXT_MEM_COUNT.value) + 1}
    for section in enumerate(elf.iter_sections()):
        idx = get_region_for_section(section)
        if idx != LlextMem.LLEXT_MEM_NULL.value:
            region_for_sects[idx].append(section)

    ordered_sections = [elf.get_section(0)]
    for mem_idx in range(LlextMem.LLEXT_MEM_COUNT.value + 1):
        ordered_sections.extend(region_for_sects[mem_idx])

    # Map section indices to their new indices (for sh_link)
    index_mapping = {}
    for i, section in enumerate(ordered_sections):
        index_mapping[elf.get_section_index(section.name)] = i

    # Write out sections and build updated section header table
    for i, section in enumerate(ordered_sections):
        if section.name == '.shstrtab':
            e_shstrndx = i

        # Accomodate for alignment
        sh_addralign = section['sh_addralign']
        # How many bytes we overshot alignment by
        past_by = f.tell() % sh_addralign if sh_addralign != 0 else 0
        if past_by > 0:
            f.seek(f.tell() + (sh_addralign - past_by))

        # Write section header
        sht += section.header.as_bytearray()
        write_field_to_header_bytearr(elf, sht, 'sh_offset', f.tell(), i)
        if section['sh_type'] == 'SHT_REL' or section['sh_type'] == 'SHT_RELA' \
            or section['sh_type'] == 'SHT_SYMTAB':
            sh_link = index_mapping[section['sh_link']]
            write_field_to_header_bytearr(elf, sht, 'sh_link', sh_link, i)

        # Write section data
        bak.seek(section['sh_offset'])
        data = bak.read(section['sh_size'])
        f.write(data)


    # Could I possibly rewrite arcextmap to use one loop
    # building the header table as I go

    # Update ELF header




def main():
    args = parse_args()

    logging.basicConfig(format=LOGGING_FORMAT)

    if args.debug:
        logger.setLevel(logging.DEBUG)
    else:
        logger.setLevel(logging.WARNING)

    if not os.path.isfile(args.file):
        logger.error(f'Cannot find file {args.file}, exiting...')
        sys.exit(1)

    with open(args.file, 'rb') as f:
        try:
            ELFFile(f)
        except Exception:
            logger.error(f'File {args.file} is not a valid ELF file, exiting...')
            sys.exit(1)

    logger.debug(f'File to strip .arcextmap.*: {args.file}')

    try:
        # Copy extension.llext to extension.llext.bak
        shutil.copy(args.file, f'{args.file}.bak')

        # Read from extension.llext.bak
        with open(f'{args.file}.bak', 'rb') as bak:
            # Write sections to keep one by one to extension.llext
            with open(args.file, 'wb') as f:
                reorder_sections(f, bak)

        os.remove(f'{args.file}.bak')
    except Exception as e:
        logger.error(f'An error occurred while processing the file: {e}')
        sys.exit(1)


if __name__ == "__main__":
    main()
