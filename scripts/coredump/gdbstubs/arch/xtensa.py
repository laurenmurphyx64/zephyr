#!/usr/bin/env python3
#
# Copyright (c) 2021 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0

import binascii
import logging
import struct
import sys

from enum import Enum
from gdbstubs.gdbstub import GdbStub

logger = logging.getLogger("gdbstub")


class GdbRegDesc():
    UNKNOWN = 0
    SAMPLE_CONTROLLER = 1
    ESP32 = 2
    INTEL_ADSP_CAVS15 = 3
    INTEL_ADSP_CAVS18 = 4
    INTEL_ADSP_CAVS20 = 5
    INTEL_ADSP_CAVS25 = 6


# The previous version of this script didn't need to know
# what toolchain Zephyr was built with; it assumed sample_controller
# was built with the Zephyr SDK and ESP32 with Espressif's.
# However, if a SOC can be built by two separate toolchains,
# there is a chance that the GDBs provided by the toolchains will
# assign different indices to the same registers. For example, the
# Intel ADSP family of SOCs can be built with both Zephyr's
# SDK and Cadence's XCC toolchain. For the APL SOC,
# the SDK's GDB assigns PC the index 0, while XCC's GDB assigns
# it the index 32.
#
# The Espressif value isn't really required, since ESP32 can
# only be built with Espressif's toolchain, but it's included for
# completeness.
class XtensaToolchain():
    UNKNOWN = 0
    ZEPHYR = 1
    XCC = 2
    ESPRESSIF = 3


def get_toolchain(toolchain):
    if not toolchain:
        logger.error("Tried to read Xtensa coredump without -t or " + \
            "-toolchain, exiting...")
        sys.exit(1)
    elif toolchain == "zephyr":
        return XtensaToolchain.ZEPHYR
    elif toolchain == "xcc":
        return XtensaToolchain.XCC
    elif toolchain == "espressif":
        return XtensaToolchain.ESPRESSIF
    else:
        return XtensaToolchain.UNKNOWN


def get_soc_definition(soc, toolchain):
    if soc == GdbRegDesc.SAMPLE_CONTROLLER:
        return GdbRegDesc_SampleController
    elif soc == GdbRegDesc.ESP32:
        return GdbRegDesc_ESP32
    elif soc in [GdbRegDesc.INTEL_ADSP_CAVS15, GdbRegDesc.INTEL_ADSP_CAVS18,
             GdbRegDesc.INTEL_ADSP_CAVS20, GdbRegDesc.INTEL_ADSP_CAVS25]:
        if toolchain == XtensaToolchain.ZEPHYR:
            return GdbRegDesc_Intel_Adsp_CAVS_Zephyr
        elif toolchain == XtensaToolchain.XCC:
            return GdbRegDesc_Intel_Adsp_CAVS_XCC
        elif toolchain == XtensaToolchain.ESPRESSIF:
            logger.error("Can't use espressif toolchain with CAVS. " +
                "Use zephyr or xcc instead. Exiting...")
            sys.exit(1)
        else:
            raise NotImplementedError
    else:
        raise NotImplementedError


class ExceptionCodes():
    # Matches arch/xtensa/core/fatal.c->z_xtensa_exccause
    ILLEGAL_INSTRUCTION = 0
    # Syscall not fatal
    INSTR_FETCH_ERROR = 2
    LOAD_STORE_ERROR = 3
    # Level-1 interrupt not fatal
    ALLOCA = 5
    DIVIDE_BY_ZERO = 6
    PRIVILEGED = 8
    LOAD_STORE_ALIGNMENT = 9
    INSTR_PIF_DATA_ERROR = 12
    LOAD_STORE_PIF_DATA_ERROR = 13
    INSTR_PIF_ADDR_ERROR = 14
    LOAD_STORE_PIF_ADDR_ERROR = 15
    INSTR_TLB_MISS = 16
    INSTR_TLB_MULTI_HIT = 17
    INSTR_FETCH_PRIVILEGE = 18
    INST_FETCH_PROHIBITED = 20
    LOAD_STORE_TLB_MISS = 24
    LOAD_STORE_TLB_MULTI_HIT = 25
    LOAD_STORE_PRIVILEGE = 26
    LOAD_PROHIBITED = 28
    STORE_PROHIBITED = 29
    # Coprocessor disabled spans 32 - 39
    # Others (reserved / unknown) map to default signal


class GdbStub_Xtensa(GdbStub):

    GDB_SIGNAL_DEFAULT = 7

    # Mapping based on section 4.4.1.5 of the
    # Xtensa ISA Reference Manual (Table 4–64. Exception Causes)
    # Somewhat arbitrary; included only because GDB requests it
    GDB_SIGNAL_MAPPING = {
        ExceptionCodes.ILLEGAL_INSTRUCTION: 4,
        ExceptionCodes.INSTR_FETCH_ERROR: 7,
        ExceptionCodes.LOAD_STORE_ERROR: 11,
        ExceptionCodes.ALLOCA: 7,
        ExceptionCodes.DIVIDE_BY_ZERO: 8,
        ExceptionCodes.PRIVILEGED: 11,
        ExceptionCodes.LOAD_STORE_ALIGNMENT: 7,
        ExceptionCodes.INSTR_PIF_DATA_ERROR: 7,
        ExceptionCodes.LOAD_STORE_PIF_DATA_ERROR: 7,
        ExceptionCodes.INSTR_PIF_ADDR_ERROR: 11,
        ExceptionCodes.LOAD_STORE_PIF_ADDR_ERROR: 11,
        ExceptionCodes.INSTR_TLB_MISS: 11,
        ExceptionCodes.INSTR_TLB_MULTI_HIT: 11,
        ExceptionCodes.INSTR_FETCH_PRIVILEGE: 11,
        ExceptionCodes.INST_FETCH_PROHIBITED: 11,
        ExceptionCodes.LOAD_STORE_TLB_MISS: 11,
        ExceptionCodes.LOAD_STORE_TLB_MULTI_HIT: 11,
        ExceptionCodes.LOAD_STORE_PRIVILEGE: 11,
        ExceptionCodes.LOAD_PROHIBITED: 11,
        ExceptionCodes.STORE_PROHIBITED: 11,
    }

    reg_fmt = "<I"

    def __init__(self, logfile, elffile, toolchain):
        super().__init__(logfile=logfile, elffile=elffile)
        self.toolchain = get_toolchain(toolchain)
        self.registers = None
        self.exception_code = None
        self.gdb_signal = self.GDB_SIGNAL_DEFAULT

        self.parse_arch_data_block()
        self.compute_signal()


    def parse_arch_data_block(self):
        arch_data_blk = self.logfile.get_arch_data()['data']

        # Get SOC in order to get correct format for unpack
        self.soc = bytearray(arch_data_blk)[0]
        self.gdbRegDesc = get_soc_definition(self.soc, self.toolchain)
        tu = struct.unpack(self.gdbRegDesc.ARCH_DATA_BLK_STRUCT, arch_data_blk)

        self.registers = dict()

        self.version = tu[1]

        self.map_registers(tu)


    def map_registers(self, tu):
        i = 2
        for r in self.gdbRegDesc.RegNum:
            regNum = r.value
            # Dummy WINDOWBASE and WINDOWSTART to enable GDB
            # without dumping them and all AR registers;
            # avoids interfering with interrupts / exceptions
            if r == self.gdbRegDesc.RegNum.WINDOWBASE:
                self.registers[regNum] = 0
            elif r == self.gdbRegDesc.RegNum.WINDOWSTART:
                self.registers[regNum] = 1
            else:
                if r == self.gdbRegDesc.RegNum.EXCCAUSE:
                    self.exception_code = tu[i]
                self.registers[regNum] = tu[i]
            i += 1


    def compute_signal(self):
        sig = self.GDB_SIGNAL_DEFAULT
        code = self.exception_code

        if code is None:
            sig = self.GDB_SIGNAL_DEFAULT

        # Map cause to GDB signal number
        if code in self.GDB_SIGNAL_MAPPING:
            sig = self.GDB_SIGNAL_MAPPING[code]
        # Coprocessor disabled code spans 32 - 39
        elif 32 <= code <= 39:
            sig = 8

        self.gdb_signal = sig


    def handle_register_group_read_packet(self):
        idx = 0
        pkt = b''

        GDB_G_PKT_MAX_REG = \
            max([regNum.value for regNum in self.gdbRegDesc.RegNum])

        # We try to send as many of the registers listed
        # as possible, but we are constrained by the
        # maximum length of the g packet
        while idx <= GDB_G_PKT_MAX_REG and idx * 4 < self.gdbRegDesc.SOC_GDB_GPKT_BIN_SIZE:
            if idx in self.registers:
                bval = struct.pack(self.reg_fmt, self.registers[idx])
                pkt += binascii.hexlify(bval)
            else:
                # Register not in coredump -> unknown value
                # Send in "xxxxxxxx"
                pkt += b'x' * 8

            idx += 1

        self.put_gdb_packet(pkt)

    # (ESP32 and sample_controller GDB don't send p, but others do)
    def handle_register_single_read_packet(self, pkt):
        # format is pXX, where XX is the hex representation of the idx
        regIdx = int('0x' + pkt[1:].decode('utf8'), 16)
        try:
            bval = struct.pack(self.reg_fmt, self.registers[regIdx])
            self.put_gdb_packet(binascii.hexlify(bval))
        except KeyError:
            self.put_gdb_packet(b'x' * 8)


# The following classes map registers to their index used by
# the GDB of a specific toolchain. See xtensa_config.c.

# WARNING: IF YOU CHANGE THE ORDER OF THE REGISTERS IN ONE
# TOOLCHAIN'S MAPPING, YOU MUST CHANGE THE ORDER TO MATCH IN THE OTHERS
# AND IN arch/xtensa/core/coredump.c's xtensa_arch_block.r.
# See map_registers.

# For the same reason, even though the WINDOWBASE and WINDOWSTART
# values are dummied by this script, they have to be last in the
# mapping below.


# sample_controller is unique to Zephyr SDK
# sdk-ng -> overlays/xtensa_sample_controller/gdb/gdb/xtensa-config.c
class GdbRegDesc_SampleController:
    ARCH_DATA_BLK_STRUCT = '<BHIIIIIIIIIIIIIIIIIIIIII'

    # This fits the maximum possible register index (110).
    # Unlike on ESP32 GDB, there doesn't seem to be an
    # actual hard limit to how big the g packet can be.
    SOC_GDB_GPKT_BIN_SIZE = 444

    class RegNum(Enum):
        PC = 0
        EXCCAUSE = 77
        EXCVADDR = 83
        SAR = 33
        PS = 38
        SCOMPARE1 = 39
        A0 = 89
        A1 = 90
        A2 = 91
        A3 = 92
        A4 = 93
        A5 = 94
        A6 = 95
        A7 = 96
        A8 = 97
        A9 = 98
        A10 = 99
        A11 = 100
        A12 = 101
        A13 = 102
        A14 = 103
        A15 = 104
        # LBEG, LEND, and LCOUNT not on sample_controller
        WINDOWBASE = 34
        WINDOWSTART = 35


# ESP32 is unique to espressif toolchain
# espressif xtensa-overlays -> xtensa_esp32/gdb/gdb/xtensa-config.c
class GdbRegDesc_ESP32:
    ARCH_DATA_BLK_STRUCT = '<BHIIIIIIIIIIIIIIIIIIIIIIIII'

    # Maximum index register that can be sent in a group packet is
    # 104, which prevents us from sending An registers directly.
    # We get around this by assigning each An in the dump to ARn
    # and setting WINDOWBASE to 0 and WINDOWSTART to 1; ESP32 GDB
    # will recalculate the corresponding An.
    SOC_GDB_GPKT_BIN_SIZE = 420

    class RegNum(Enum):
        PC = 0
        EXCCAUSE = 143
        EXCVADDR = 149
        SAR = 68
        PS = 73
        SCOMPARE1 = 76
        AR0 = 1
        AR1 = 2
        AR2 = 3
        AR3 = 4
        AR4 = 5
        AR5 = 6
        AR6 = 7
        AR7 = 8
        AR8 = 9
        AR9 = 10
        AR10 = 11
        AR11 = 12
        AR12 = 13
        AR13 = 14
        AR14 = 15
        AR15 = 16
        LBEG = 65
        LEND = 66
        LCOUNT = 67
        WINDOWBASE = 69
        WINDOWSTART = 70


# sdk-ng -> overlays/xtensa_intel_apl/gdb/gdb/xtensa-config.c
class GdbRegDesc_Intel_Adsp_CAVS_Zephyr:
    ARCH_DATA_BLK_STRUCT = '<BHIIIIIIIIIIIIIIIIIIIIIIIII'

    # There seems to be a packet size restriction in the sense
    # that if you send all the registers below (up to index 173)
    # GDB incorrectly assigns 0 to EXCCAUSE / EXCVADDR... for some
    # reason. Since APL GDB sends p packets for every An register
    # regardless of whether it was sent in the g packet, I decided to
    # somewhat arbitrarily shrink it to include up to A1, which fixed
    # the issue.
    SOC_GDB_GPKT_BIN_SIZE = 640


    class RegNum(Enum):
        PC = 0
        EXCCAUSE = 148
        EXCVADDR = 154
        SAR = 68
        PS = 74
        SCOMPARE1 = 77
        A0 = 158
        A1 = 159
        A2 = 160
        A3 = 161
        A4 = 162
        A5 = 163
        A6 = 164
        A7 = 165
        A8 = 166
        A9 = 167
        A10 = 168
        A11 = 169
        A12 = 170
        A13 = 171
        A14 = 172
        A15 = 173
        LBEG = 65
        LEND = 66
        LCOUNT = 67
        WINDOWBASE = 70
        WINDOWSTART = 71


# Reverse-engineered from:
# sof -> src/debug/gdb/gdb.c
# sof -> src/arch/xtensa/include/xtensa/specreg.h
class GdbRegDesc_Intel_Adsp_CAVS_XCC:
    ARCH_DATA_BLK_STRUCT = '<BHIIIIIIIIIIIIIIIIIIIIIIIII'

    # xt-gdb doesn't appear to use the g packet at all
    SOC_GDB_GPKT_BIN_SIZE = 0


    class RegNum(Enum):
        PC = 32
        EXCCAUSE = 744
        EXCVADDR = 750
        SAR = 515
        PS = 742
        SCOMPARE1 = 524
        A0 = 256
        A1 = 257
        A2 = 258
        A3 = 259
        A4 = 260
        A5 = 261
        A6 = 262
        A7 = 263
        A8 = 264
        A9 = 265
        A10 = 266
        A11 = 267
        A12 = 268
        A13 = 269
        A14 = 270
        A15 = 271
        LBEG = 512
        LEND = 513
        LCOUNT = 514
        WINDOWBASE = 584
        WINDOWSTART = 585
