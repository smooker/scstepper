/*
 * firmware_crc.c — CRC32 word at the last word of program flash.
 *
 * Placed at 0x0803FFFC (FW_CRC memory region, linker script).
 * The placeholder 0xDEADBEEF is patched by scripts/crc_patch.py
 * after every build.  Do not change the value here manually.
 *
 * Algorithm: STM32 CRC32 peripheral (poly=0x04C11DB7, init=0xFFFFFFFF,
 * no reflection) over [0x08000000 .. 0x0803FFFC) = 65535 words.
 */

#include <stdint.h>

const uint32_t __fw_crc
    __attribute__((section(".fw_crc"), used)) = 0xDEADBEEF;
