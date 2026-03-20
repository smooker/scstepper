/*
 * firmware_crc.c — 128-byte info+CRC block at end of program flash.
 *
 * Placed at 0x0803FF80 (FW_CRC memory region, 32 x uint32):
 *   Words  0-30 (124 bytes): ASCII label string, null-padded
 *   Word  31    (  4 bytes): STM32 CRC32 word
 *
 * Both fields are patched by scripts/crc_patch.py after every build:
 *   - label: "SMOOKER AND CLAUDE YYYY-MM-DD HH:MM"  (build timestamp)
 *   - crc:   CRC32 over [0x08000000 .. 0x0803FF80) = 65504 words
 *
 * Algorithm: STM32 CRC32 peripheral (poly=0x04C11DB7, init=0xFFFFFFFF,
 * no reflection).  Firmware reads these back at boot (see main.c).
 */

#include <stdint.h>

struct fw_info {
    char     label[124];   /* 31 uint32 — ASCII build label, null-padded */
    uint32_t crc;          /* 32nd uint32 — CRC32, patched by crc_patch.py */
};

const struct fw_info __fw_info
    __attribute__((section(".fw_crc"), used)) = {
    .label = "SMOOKER AND CLAUDE XXXX-XX-XX",  /* placeholder, patched */
    .crc   = 0xDEADBEEF,
};
