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

class LlextRegion():
    def __init__(self):
        self.sections = []
        self.sh_type = None
        self.sh_flags = 0
        self.sh_addr = 0
        self.sh_offset = 0
        self.sh_size = 0
        self.sh_info = None

    def add_section(self, section):
        if not self.sections: # First section in the region
            self.sh_type = section['sh_type']
            self.sh_flags = section['sh_flags']
            self.sh_addr = section['sh_addr']
            self.sh_offset = section['sh_offset']
            self.sh_size = section['sh_size']
            self.sh_info = section['sh_info']
        else:
            self.sh_addr = min(self.sh_addr, section['sh_addr'])
            self.sh_offset = min(self.sh_offset, section['sh_offset'])
            top_of_region = max(self.sh_offset + self.sh_size, \
                                section['sh_offset'] + section['sh_size'])
            self.sh_size = top_of_region - self.sh_offset
        self.sections.append(section)

    def bottom(self, field):
        return getattr(self, field) + self.sh_info

    def top(self, field):
        return getattr(self, field) + self.sh_size - 1

    def overlaps(self, field, other):
        return (self.bottom(field) <= other.top(field) and \
                 self.top(field) >= other.bottom(field)) or \
                (other.bottom(field) <= self.top(field) and \
                 other.top(field) >= self.bottom(field))

def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument('file', help='Object file')
    parser.add_argument('--debug', action='store_true',
                        help='Print extra debugging information')

    return parser.parse_args()

def get_region_for_section(elf, section):
    if section['sh_type'] == 'SHT_SYMTAB' and elf.header['e_type'] == 'ET_REL':
        mem_idx = LlextMem.LLEXT_MEM_SYMTAB.value
    elif section['sh_type'] == 'SHT_DYNSYM' and elf.header['e_type'] == 'ET_DYN':
        mem_idx = LlextMem.LLEXT_MEM_SYMTAB.value
    elif section['sh_type'] == 'SHT_STRTAB':
        mem_idx = LlextMem.LLEXT_MEM_STRTAB.value
    elif section['sh_type'] == 'SHT_SHSTRTAB':
        mem_idx = LlextMem.LLEXT_MEM_SHSTRTAB.value
    elif section["sh_type"] == 'SHT_NOBITS':
        mem_idx = LlextMem.LLEXT_MEM_BSS.value
    elif section['sh_type'] == 'SHT_PROGBITS':
        if section['sh_flags'] & SH_FLAGS.SHF_EXECINSTR:
            mem_idx = LlextMem.LLEXT_MEM_TEXT.value
        elif section['sh_flags'] & SH_FLAGS.SHF_WRITE:
            mem_idx = LlextMem.LLEXT_MEM_DATA.value
        else:
            mem_idx = LlextMem.LLEXT_MEM_RODATA.value
    elif section['sh_type'] == 'SHT_PREINIT_ARRAY':
        mem_idx = LlextMem.LLEXT_MEM_PREINIT.value
    elif section['sh_type'] == 'SHT_INIT_ARRAY':
        mem_idx = LlextMem.LLEXT_MEM_INIT.value
    elif section['sh_type'] == 'SHT_FINI_ARRAY':
        mem_idx = LlextMem.LLEXT_MEM_FINI.value
    else:
        mem_idx = LlextMem.LLEXT_MEM_COUNT.value

    if section.name == '.exported_sym':
        mem_idx = LlextMem.LLEXT_MEM_EXPORT.value

    if mem_idx == LlextMem.LLEXT_MEM_COUNT.value:
        logger.debug(f'Section {section.name} skipped')
    else:
        logger.debug(f'Section {section.name} assigned to region {LlextMem(mem_idx).value}')

    return mem_idx


def reorder_sections(f, bak):
    elf = ELFFile(bak)

    elfh = bytearray()

    e_shentsize = elf.header['e_shentsize']

    e_shstrndx = 0
    e_shoff = 0

    sht = bytearray()

    # Read in ELF header
    bak.seek(0) # elftools moves the file pointer
    elfh += bak.read(elf.header['e_ehsize'])

    # Write out to move the file pointer forward past the ELF header
    # We will update it later
    f.write(elfh)

    # Read in section header table
    bak.seek(elf.header['e_shoff'])
    bak_sht = bak.read(elf.header['e_shnum'] * elf.header['e_shentsize'])

    # Get list of sections ordered by region
    region_for_sects = [None] * (LlextMem.LLEXT_MEM_COUNT.value + 1)
    for i in range(LlextMem.LLEXT_MEM_COUNT.value + 1):
        region_for_sects[i] = LlextRegion()

    for i, section in enumerate(elf.iter_sections(), start=1):
        region_idx = get_region_for_section(elf, section)
        region_for_sects[region_idx].add_section(section)

    # Determine if sections need to be reordered
    # (See llext_map_sections)
    to_reorder = False
    for i in range(LlextMem.LLEXT_MEM_COUNT.value):
        for j in range(i + 1, LlextMem.LLEXT_MEM_COUNT.value + 1):
            if region_for_sects[i].sh_type == 'SHT_NULL' or \
               region_for_sects[i].sh_size == 0 or \
               region_for_sects[j].sh_type == 'SHT_NULL' or \
               region_for_sects[j].sh_size == 0:
                continue

            if (i == LlextMem.LLEXT_MEM_DATA.value or \
                i == LlextMem.LLEXT_MEM_RODATA.value) and \
                j == LlextMem.LLEXT_MEM_EXPORT.value:
                continue

            if i == LlextMem.LLEXT_MEM_EXPORT.value or \
               j == LlextMem.LLEXT_MEM_EXPORT.value:
                continue

            if (elf.header['e_type'] == 'ET_DYN') and \
                (region_for_sects[i].sh_flags & SH_FLAGS.SHF_ALLOC) and \
                (region_for_sects[j].sh_flags & SH_FLAGS.SHF_ALLOC):
                if region_for_sects[i].overlaps('sh_addr', region_for_sects[j]):
                    logger.debug(f'Region {i} VMA range overlaps with {j}')
                    to_reorder = True
                    break

            if i == LlextMem.LLEXT_MEM_BSS.value or \
               j == LlextMem.LLEXT_MEM_BSS.value:
                continue

            if region_for_sects[i].overlaps('sh_offset', region_for_sects[j]):
                logger.debug(f'Region {i} ELF file range overlaps with {j}')
                to_reorder = True
                break

    if not to_reorder:
        logger.info('No sections to reorder, exiting...')
        return to_reorder

    # Create ordered list of sections
    ordered_sections = [elf.get_section(0)]
    for mem_idx in range(LlextMem.LLEXT_MEM_COUNT.value + 1):
        ordered_sections.extend(region_for_sects[mem_idx].sections)

    # Map unordered section indices to ordered indices (for sh_link)
    index_mapping = {}
    for i, section in enumerate(ordered_sections):
        index_mapping[elf.get_section_index(section.name)] = i

    # Write out sections and build updated section header table
    for i, section in enumerate(ordered_sections):
        if section.name == '.shstrtab':
            e_shstrndx = i
            logger.debug(f'e_shstrndx = {e_shstrndx}')

        # Accomodate for alignment
        sh_addralign = section['sh_addralign']
        # How many bytes we overshot alignment by
        past_by = f.tell() % sh_addralign if sh_addralign != 0 else 0
        if past_by > 0:
            f.seek(f.tell() + (sh_addralign - past_by))
            logger.debug(f'Adjusting section offset for sh_addralign {sh_addralign}')
        logger.debug(f'Section {i} at 0x{f.tell():x} with name {section.name}')

        # Write section header
        bak_sht_idx = elf.get_section_index(section.name)
        sht += bak_sht[bak_sht_idx * e_shentsize:(bak_sht_idx + 1) * e_shentsize]
        write_field_to_header_bytearr(elf, sht, 'sh_offset', f.tell(), i)

        if section['sh_type'] == 'SHT_REL' or section['sh_type'] == 'SHT_RELA' \
            or section['sh_type'] == 'SHT_SYMTAB':
            sh_link = index_mapping[section['sh_link']]
            write_field_to_header_bytearr(elf, sht, 'sh_link', sh_link, i)
            logger.debug(f'\tsh_link updated from {section['sh_link']} to {sh_link}')

            if section['sh_type'] != 'SHT_SYMTAB':
                sh_info = index_mapping[section['sh_info']]
                write_field_to_header_bytearr(elf, sht, 'sh_info', sh_info, i)

        # Write section data
        bak.seek(section['sh_offset'])
        data = bak.read(section['sh_size'])
        f.write(data)

    # Write out section header table
    e_shoff = f.tell()
    logger.debug(f'e_shoff = 0x{e_shoff:x}')

    f.write(sht)

    # Update and rewrite out ELF header
    write_field_to_header_bytearr(elf, elfh, 'e_shoff', e_shoff)
    write_field_to_header_bytearr(elf, elfh, 'e_shstrndx', e_shstrndx)

    f.seek(0)
    f.write(elfh)

    return to_reorder

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

    logger.debug(f'File to reorder for LLEXT: {args.file}')

    try:
        # Copy extension.llext to extension.llext.bak
        shutil.copy(args.file, f'{args.file}.bak')

        reordered = False
        # Read from extension.llext.bak
        with open(f'{args.file}.bak', 'rb') as bak:
            # Write sections to keep one by one to extension.llext
            with open(args.file, 'wb') as f:
                reordered = reorder_sections(f, bak)

        if not reordered:
            os.remove(f'{args.file}')
            os.rename(f'{args.file}.bak', args.file)
        else:
            os.remove(f'{args.file}.bak')
    except Exception as e:
        logger.error(f'An error occurred while processing the file: {e}')
        sys.exit(1)


if __name__ == "__main__":
    main()
