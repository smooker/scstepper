#!/usr/bin/env python3
"""
crc_patch.py — patch firmware CRC32 word into BIN and HEX after build.

Usage: python3 scripts/crc_patch.py <elf> <bin> <hex>

Layout (STM32F411, this project):
  FLASH Sectors 0-5: 0x08000000 - 0x0803FFFF  (256 KB program area)
  FW_CRC word:       0x0803FFFC - 0x0803FFFF  (last 4 bytes)
  EEPROM Sectors 6-7: 0x08040000+             (not touched)

CRC covers [0x08000000 .. 0x0803FFFC) = 65535 x 32-bit words.
Flash bytes between firmware end and CRC word are 0xFF (erased) —
included in CRC, must match at runtime.

Algorithm matches STM32 CRC32 peripheral:
  poly = 0x04C11DB7, init = 0xFFFFFFFF, no input/output reflection.
  Words are fed as little-endian uint32 (ARM memory layout).
"""

import sys
import struct
import subprocess

FLASH_BASE   = 0x08000000
FW_CRC_ADDR  = 0x0803FFFC
FW_CRC_OFFSET = FW_CRC_ADDR - FLASH_BASE   # = 0x3FFFC = 262140
FLASH_SIZE    = 256 * 1024                  # = 262144 bytes (Sectors 0-5)


def stm32_crc32(data: bytes) -> int:
    """STM32 CRC32: poly=0x04C11DB7, init=0xFFFFFFFF, no reflection."""
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

    # Read BIN — should be exactly FLASH_SIZE bytes (linker places .fw_crc at end)
    with open(bin_path, 'rb') as f:
        data = bytearray(f.read())

    if len(data) != FLASH_SIZE:
        print("crc_patch: ERROR: BIN size {} != {} (expected 256KB)".format(
              len(data), FLASH_SIZE))
        print("           Check linker script — .fw_crc must be in FW_CRC region.")
        sys.exit(1)

    # Compute CRC over everything before the CRC word
    fw_bytes = bytes(data[:FW_CRC_OFFSET])
    crc = stm32_crc32(fw_bytes)

    old_crc = struct.unpack_from('<I', data, FW_CRC_OFFSET)[0]
    struct.pack_into('<I', data, FW_CRC_OFFSET, crc)

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
          crc, old_crc, FW_CRC_ADDR))


if __name__ == '__main__':
    main()
