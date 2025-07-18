#!/usr/bin/env python3
#
# Copyright (c) 2025 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

'''Strip arcextmap names from section header string table

The MWDT strip utility does not strip the names of stripped sections from the
section header string table. This script is a workaround to remove unused .arcextmap.*
names from the section header string table, dramatically shrinking the file size. This is
necessary for boards where MWDT CCAC emits debugging sections like .arcextmap.*,
as even the smallest extension (<1k) will have a >16KB section header string table.
'''

import argparse
import logging
import os
import sys
import shutil

from elftools.elf.elffile import ELFFile
from llext_elf_editor import write_field_in_struct_bytearr

logger = logging.getLogger('strip-arcextmap-names')
LOGGING_FORMAT = '[%(levelname)s][%(name)s] %(message)s'

def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)

    parser.add_argument('file', help='Object file')
    parser.add_argument('--debug', action='store_true',
                        help='Print extra debugging information')

    return parser.parse_args()

def strip_arcextmap(f, bak):
    elf = ELFFile(bak)

    if elf.header['e_type'] == 'ET_CORE':
        logger.warning('Script not applicable to ET_CORE files')
        return

    elfh = bytearray()

    e_shentsize = elf.header['e_shentsize']

    e_shoff = 0

    sht = bytearray()
    pht = bytearray()

    sht_sh_offsets = []
    sht_sh_names = []

    shstrtab = bytearray()

    # Read in ELF header (to move file pointer forward)
    bak.seek(0)
    elfh += bak.read(elf.header['e_ehsize'])
    f.write(elfh)

    # Read in program header table (if present) and write it out
    if elf.header['e_phnum'] > 0:
        bak.seek(elf.header['e_phoff'])
        pht += bak.read(elf.header['e_phnum'] * elf.header['e_phentsize'])
        f.write(pht)

    # Read in section header table
    bak.seek(elf.header['e_shoff'])
    sht += bak.read(elf.header['e_shnum'] * e_shentsize)

    # Rebuild the section header string table
    sh_name = 0
    for i, section in enumerate(elf.iter_sections()):
        shstrtab += section.name.encode('utf-8') + b'\x00'
        sht_sh_names.append(sh_name)
        sh_name += len(section.name) + 1

    shstrtab_sh_size = len(shstrtab)
    logger.debug('.shstrtab sh_size shrunk from {} to {} bytes'.format(
            elf.get_section(elf.header['e_shstrndx'])['sh_size'], shstrtab_sh_size))

    # Write out sections and update headers
    for i, section in enumerate(elf.iter_sections()):
        if elf.header['e_shstrndx'] == i:
            section_data = shstrtab
            write_field_in_struct_bytearr(elf, sht, 'sh_size', shstrtab_sh_size, i)
        else:
            bak.seek(section['sh_offset'])
            section_data = bak.read(section['sh_size'])

        # Accomodate for alignment
        sh_addralign = section['sh_addralign']
        # How many bytes we overshot alignment by
        past_by = f.tell() % sh_addralign if sh_addralign != 0 else 0
        if past_by > 0:
            logger.debug('Padding section {} by {} bytes to align by {}'.format( \
                i, sh_addralign - past_by, sh_addralign))
            f.seek(f.tell() + (sh_addralign - past_by))

        sht_sh_offsets.append(f.tell())

        logger.debug('Section {} read from {}, written at {}'.format(
            i, hex(section['sh_offset']), hex(sht_sh_offsets[i])))
        f.write(section_data)

        # Update entry in section header table
        write_field_in_struct_bytearr(elf, sht, 'sh_name', sht_sh_names[i], i)
        write_field_in_struct_bytearr(elf, sht, 'sh_offset', sht_sh_offsets[i], i)

        name_end = shstrtab[sht_sh_names[i]:].find(b'\x00')
        if name_end != -1:
            name = shstrtab[sht_sh_names[i]:sht_sh_names[i] + name_end].decode('utf-8')
            logger.debug('Section {} name read as {}, written out as {}'.format(
                i, section.name, name))
        else:
            logger.error(f'Unable to find null terminated string for section {i} in shrunk shstrtab')

    # Write the section header table to the file
    e_shoff = f.tell()
    f.write(sht)

    f.truncate() # File has shrunk signifcantly, truncate to remove excess

    # Modify the ELF header
    write_field_in_struct_bytearr(elf, elfh, 'e_shoff', e_shoff)

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

    try:
        # Back up extension.llext
        shutil.copy(args.file, f'{args.file}.bak')

        # Read from extension.llext.bak
        with open(f'{args.file}.bak', 'rb') as bak:
            # Write out to extension.llext
            with open(args.file, 'wb') as f:
                strip_arcextmap(f, bak)

        os.remove(f'{args.file}.bak')
    except Exception as e:
        logger.error(f'An error occurred while processing the file: {e}')
        sys.exit(1)

if __name__ == "__main__":
    main()
