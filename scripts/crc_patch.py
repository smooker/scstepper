#!/usr/bin/env python3
"""
crc_patch.py — patch firmware label + CRC32 word into BIN and HEX after build.

Usage: python3 scripts/crc_patch.py <elf> <bin> <hex>

Flash layout (STM32F411, this project):
  Sectors 0-5:  0x08000000 - 0x0803FF7F  (program area, 256KB - 128B)
  FW_CRC block: 0x0803FF80 - 0x0803FFFF  (128B = 32 x uint32)
    Words  0-30: ASCII label, null-padded  (124 bytes)
    Word   31:   CRC32                     (  4 bytes, @ 0x0803FFFC)
  EEPROM:       0x08040000+               (not in BIN, not touched by fl)

CRC covers [0x08000000 .. 0x0803FF80) = 65504 x 32-bit words.
Flash bytes between firmware end and 0x0803FF80 are 0xFF (erased) —
included in CRC, must match at runtime.

Algorithm matches STM32 CRC32 peripheral:
  poly = 0x04C11DB7, init = 0xFFFFFFFF, no input/output reflection.
  Words are fed as little-endian uint32 (ARM memory layout).

Label format: "SMOOKER AND CLAUDE YYYY-MM-DD HH:MM"  (null-padded to 124B)
Reading the block in GDB:
  x/s  0x0803FF80    — label as string
  x/xw 0x0803FFFC   — CRC word (little-endian on ARM)
  x/32wx 0x0803FF80  — all 32 words (bytes reversed per word due to LE)
"""

import sys
import struct
import subprocess
from datetime import datetime

FLASH_BASE     = 0x08000000
FW_CRC_BASE    = 0x0803FF80          # start of 128-byte block
FW_CRC_OFFSET  = FW_CRC_BASE - FLASH_BASE   # = 0x3FF80 = 262016
LABEL_SIZE     = 124                 # 31 x uint32
CRC_OFFSET     = FW_CRC_OFFSET + LABEL_SIZE  # = 0x3FFFC = 262140
FLASH_SIZE     = 256 * 1024          # = 262144 bytes (Sectors 0-5)


def stm32_crc32(data: bytes) -> int:
    """STM32 CRC32: poly=0x04C11DB7, init=0xFFFFFFFF, no reflection.
    Words fed as little-endian uint32 (matching ARM memory layout)."""
    assert len(data) % 4 == 0
    crc = 0xFFFFFFFF
    for i in range(0, len(data), 4):
        word = struct.unpack_from('<I', data, i)[0]
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def main():
    if len(sys.argv) != 4:
        print("Usage: crc_patch.py <elf> <bin> <hex>")
        sys.exit(1)

    elf_path, bin_path, hex_path = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(bin_path, 'rb') as f:
        data = bytearray(f.read())

    if len(data) != FLASH_SIZE:
        print("crc_patch: ERROR: BIN size {} != {} (expected 256KB)".format(
              len(data), FLASH_SIZE))
        print("           Check linker script — .fw_crc must be in FW_CRC region.")
        sys.exit(1)

    # Build label: "SMOOKER AND CLAUDE YYYY-MM-DD HH:MM"
    now = datetime.now()
    label_str = "SMOOKER AND CLAUDE {}-{:02d}-{:02d} {:02d}:{:02d}".format(
        now.year, now.month, now.day, now.hour, now.minute)
    label_bytes = label_str.encode('ascii')[:LABEL_SIZE - 1]
    label_padded = label_bytes + b'\x00' * (LABEL_SIZE - len(label_bytes))

    # Patch label into BIN
    data[FW_CRC_OFFSET:FW_CRC_OFFSET + LABEL_SIZE] = label_padded

    # CRC over [0x08000000 .. 0x0803FF80) — firmware + 0xFF padding, excl. block
    fw_bytes = bytes(data[:FW_CRC_OFFSET])
    crc = stm32_crc32(fw_bytes)

    old_crc = struct.unpack_from('<I', data, CRC_OFFSET)[0]
    struct.pack_into('<I', data, CRC_OFFSET, crc)

    with open(bin_path, 'wb') as f:
        f.write(data)

    # Regenerate HEX from patched BIN
    subprocess.check_call([
        'arm-none-eabi-objcopy',
        '-I', 'binary', '-O', 'ihex',
        '--change-addresses', str(FLASH_BASE),
        bin_path, hex_path
    ], stderr=subprocess.DEVNULL)

    print("  CRC32: 0x{:08X}  (was 0x{:08X})  @ 0x{:08X}".format(
          crc, old_crc, FLASH_BASE + CRC_OFFSET))
    print("  label: \"{}\"  @ 0x{:08X}".format(
          label_str, FW_CRC_BASE))


if __name__ == '__main__':
    main()
