#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

'''Strip arcextmap-related info and sections.

The MWDT strip utility does not strip the names of stripped sections from the
section header string table. This script is a workaround to remove unused
names from the section header string table, shrinking the file size. This is
necessary for boards where MWDT CCAC emits debugging sections like .arcextmap.*,
as even the smallest extension (<1k) will have a >16KB section header string table.

This script is also able to remove the sections themselves, not just their names,
should they appear in the ELF. This will happen if strip is not given the
'strip unallocated' option for whatever reason.
'''

import argparse
import logging
import os
import sys
import shutil

from elftools.elf.elffile import ELFFile
from llext_elf_editor import write_field_in_struct_bytearr

logger = logging.getLogger('strip')
LOGGING_FORMAT = '[%(levelname)s][%(name)s] %(message)s'


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument('file', help='Object file')
    parser.add_argument('--debug', action='store_true',
                        help='Print extra debugging information')

    return parser.parse_args()


def strip_arcextmap(f, bak):
    '''Remove .arcextmap.* sections from the ELF file.

    This function reads in from a copy of the ELF file and copies over its
    sections, skipping any sections with names of the form .arcextmap.* It also makes
    necessary adjustments to the ELF header, section header table, and creates a
    copy of the section header string table without the unused names, which it
    writes as the last section of the file before the section header table.
    '''

    elf = ELFFile(bak)

    elfh = bytearray()

    e_shentsize = elf.header['e_shentsize']

    e_shoff = 0
    e_shnum = 0
    e_shstrndx = 0

    sht = bytearray()

    sht_sh_offsets = []
    sht_sh_names = []

    sections = [] # Sections to be copied over
    index_mapping = {} # Maps section index in original to copy

    shstrtab = bytearray()

    # Read in ELF header
    bak.seek(0) # elftools moves the file pointer
    elfh += bak.read(elf.header['e_ehsize'])

    # Move the file pointer forward past the ELF header
    # We will correct it inline later
    f.write(elfh)

    # Write out sections except .arcextmap.* and .shstrtab
    # Copy in section headers except .shstrtab
    sh_name = 0
    sh_idx = 0
    for i, section in enumerate(elf.iter_sections()):
        if not section.name.startswith('.arcextmap') and section.name != '.shstrtab':
            sections.append(section)
            index_mapping[i] = sh_idx

            shstrtab += section.name.encode('utf-8') + b'\x00'
            sht_sh_names.append(sh_name)
            sh_name += len(section.name) + 1

            bak.seek(section['sh_offset'])
            section_data = bak.read(section['sh_size'])

            # Accomodate for alignment
            sh_addralign = section['sh_addralign']
            # How many bytes we overshot alignment by
            past_by = f.tell() % sh_addralign if sh_addralign != 0 else 0
            if past_by > 0:
                f.seek(f.tell() + (sh_addralign - past_by))

            sht_sh_offsets.append(f.tell())
            f.write(section_data)

            bak.seek(elf.header['e_shoff'] + (i * e_shentsize))
            section_header = bak.read(e_shentsize)
            sht += section_header

            sh_idx += 1

    # Write .shstrtab as the last section
    # Copy in .shstrtab section header
    e_shstrndx = sh_idx
    e_shnum = sh_idx + 1

    sections.append(elf.get_section_by_name('.shstrtab'))
    index_mapping[elf.header['e_shstrndx']] = e_shstrndx

    shstrtab += '.shstrtab'.encode('utf-8') + b'\x00'
    sht_sh_names.append(sh_name)

    sht_sh_offsets.append(f.tell())
    f.write(shstrtab)

    bak.seek(elf.header['e_shoff'] + (elf.header['e_shstrndx'] * e_shentsize))
    shstrtab_header = bak.read(e_shentsize)
    sht += shstrtab_header

    # Modify the section header table
    # Adjust size of .shstrtab
    write_field_in_struct_bytearr(elf, sht, 'sh_size', len(shstrtab), e_shstrndx)

    # Adjust sh_name, sh_offset, sh_link and sh_info for each section header
    for i, section in enumerate(sections):
        write_field_in_struct_bytearr(elf, sht, 'sh_name', sht_sh_names[i], i)
        write_field_in_struct_bytearr(elf, sht, 'sh_offset', sht_sh_offsets[i], i)

        if section['sh_type'] == 'SHT_REL' or section['sh_type'] == 'SHT_RELA' \
            or section['sh_type'] == 'SHT_SYMTAB':
            sh_link = index_mapping[section['sh_link']]
            write_field_in_struct_bytearr(elf, sht, 'sh_link', sh_link, i)

            if section['sh_type'] != 'SHT_SYMTAB':
                sh_info = index_mapping[section['sh_info']]
                write_field_in_struct_bytearr(elf, sht, 'sh_info', sh_info, i)

    # Write the section header table to the file
    e_shoff = f.tell()
    f.write(sht)

    f.truncate()

    # Modify the ELF header
    write_field_in_struct_bytearr(elf, elfh, 'e_shoff', e_shoff)
    write_field_in_struct_bytearr(elf, elfh, 'e_shnum', e_shnum)
    write_field_in_struct_bytearr(elf, elfh, 'e_shstrndx', e_shstrndx)

    # Write back the ELF header
    f.seek(0)
    f.write(elfh)


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
                strip_arcextmap(f, bak)

        os.remove(f'{args.file}.bak')
    except Exception as e:
        logger.error(f'An error occurred while processing the file: {e}')
        sys.exit(1)


if __name__ == "__main__":
    main()
