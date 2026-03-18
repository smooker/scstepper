/**
 * @file eeprom_emul.h
 * @brief EEPROM Emulation using Internal Flash for STM32F411CEU6
 *
 * Uses two flash sectors for wear-leveling (page swap technique).
 * Stores uint32_t key-value pairs.
 *
 * STM32F411CEU6 Flash layout (512KB total):
 *   Sector 0: 0x08000000 - 16KB  (application)
 *   Sector 1: 0x08004000 - 16KB  (application)
 *   Sector 2: 0x08008000 - 16KB  (application)
 *   Sector 3: 0x0800C000 - 16KB  (application)
 *   Sector 4: 0x08010000 - 64KB  (application)
 *   Sector 5: 0x08020000 - 128KB (application)
 *   Sector 6: 0x08040000 - 128KB <-- EEPROM Page 0 (we use this)
 *   Sector 7: 0x08060000 - 128KB <-- EEPROM Page 1 (we use this)
 *
 * Adjust EEPROM_PAGE0_BASE / PAGE1_BASE and sector numbers to fit your app.
 */

#ifndef EEPROM_EMUL_H
#define EEPROM_EMUL_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ─── Configuration ─────────────────────────────────────────────────────── */

/** Base addresses of the two flash sectors used for emulation */
#define EEPROM_PAGE0_BASE       0x08040000UL   /* Sector 6, 128 KB */
#define EEPROM_PAGE1_BASE       0x08060000UL   /* Sector 7, 128 KB */
#define EEPROM_PAGE_SIZE        (128UL * 1024) /* 128 KB each      */

/** Corresponding FLASH_SECTOR_x values for HAL erase */
#define EEPROM_PAGE0_SECTOR     FLASH_SECTOR_6
#define EEPROM_PAGE1_SECTOR     FLASH_SECTOR_7

/** Maximum number of unique virtual addresses (keys).
 *  Keys must be in range [1 .. EEPROM_MAX_VARS]. Key 0 is reserved. */
#define EEPROM_MAX_VARS         128

/* ─── Page status words (written at offset 0 of each sector) ─────────────── */
#define PAGE_STATUS_ERASED      0xFFFFFFFFUL
#define PAGE_STATUS_RECEIVE     0xEEEEEEEEUL   /* being filled during swap */
#define PAGE_STATUS_VALID       0xAAAAAAAAUL   /* active page              */

/* ─── Return codes ───────────────────────────────────────────────────────── */
typedef enum {
    EEPROM_OK           =  0,
    EEPROM_NOT_FOUND    =  1,   /* key not present (read returns default)  */
    EEPROM_FLASH_ERROR  = -1,
    EEPROM_NO_SPACE     = -2,
    EEPROM_BAD_PARAM    = -3,
} EEPROM_Status;

/* ─── API ────────────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise EEPROM emulation.
 *         Must be called once before any read/write.
 * @retval EEPROM_OK on success.
 */
EEPROM_Status EEPROM_Init(void);

/**
 * @brief  Read a uint32_t value from virtual address @p vaddr.
 * @param  vaddr    Virtual address / key [1 .. EEPROM_MAX_VARS].
 * @param  data     Pointer to store the value.
 * @retval EEPROM_OK       – value found.
 *         EEPROM_NOT_FOUND – key never written; *data is unchanged.
 */
EEPROM_Status EEPROM_Read(uint16_t vaddr, uint32_t *data);

/**
 * @brief  Write a uint32_t value to virtual address @p vaddr.
 *         Triggers a page-swap / compaction if the active page is full.
 * @param  vaddr    Virtual address / key [1 .. EEPROM_MAX_VARS].
 * @param  data     Value to store.
 * @retval EEPROM_OK on success.
 */
EEPROM_Status EEPROM_Write(uint16_t vaddr, uint32_t data);

/**
 * @brief  Erase all EEPROM data and reinitialise (factory reset).
 * @retval EEPROM_OK on success.
 */
EEPROM_Status EEPROM_Format(void);

#endif /* EEPROM_EMUL_H */