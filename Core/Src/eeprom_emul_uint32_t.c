/**
 * @file eeprom_emul.c
 * @brief EEPROM Emulation – implementation for STM32F411CEU6
 *
 * Flash record layout (8 bytes per record):
 *   [31:16] virtual address (key)    – uint16_t
 *   [15: 0] reserved / 0x0000
 *   [63:32] data value               – uint32_t
 *
 * Both words are written in a single 64-bit sequence so the entry is
 * atomically visible: flash location starts at 0xFFFFFFFF_FFFFFFFF
 * and moves to a programmed value.  An entry with vaddr == 0xFFFF is
 * treated as empty (never written).
 *
 * Page layout:
 *   Offset 0x00 : 4-byte page status word  (PAGE_STATUS_*)
 *   Offset 0x04 : 4 bytes padding / 0xFFFFFFFF
 *   Offset 0x08 : first record  (8 bytes)
 *   Offset 0x10 : second record (8 bytes)
 *   ...
 */

#include "eeprom_emul_uint32_t.h"

/* ─── Internal sizes ─────────────────────────────────────────────────────── */

/** Each record = 8 bytes: 4-byte header word + 4-byte data word */
#define RECORD_SIZE     8U
#define HEADER_OFFSET   0U   /* offset of {vaddr,padding} within record */
#define DATA_OFFSET     4U   /* offset of data word within record        */

/** Byte offset of the first record inside a page */
#define RECORDS_START   8U   /* 4-byte status + 4-byte pad */

/** Maximum number of records that fit in one page */
#define MAX_RECORDS     ((EEPROM_PAGE_SIZE - RECORDS_START) / RECORD_SIZE)

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static inline uint32_t page_status(uint32_t base)
{
    return *(volatile uint32_t *)base;
}

static inline uint32_t record_header(uint32_t base, uint32_t index)
{
    return *(volatile uint32_t *)(base + RECORDS_START + index * RECORD_SIZE + HEADER_OFFSET);
}

static inline uint32_t record_data(uint32_t base, uint32_t index)
{
    return *(volatile uint32_t *)(base + RECORDS_START + index * RECORD_SIZE + DATA_OFFSET);
}

/** Extract virtual address from a header word */
static inline uint16_t header_to_vaddr(uint32_t header)
{
    return (uint16_t)(header >> 16);
}

/** Build a header word from a virtual address */
static inline uint32_t vaddr_to_header(uint16_t vaddr)
{
    return ((uint32_t)vaddr << 16) | 0x0000UL;
}

/* ─── Flash helpers ──────────────────────────────────────────────────────── */

static EEPROM_Status flash_unlock_wrap(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK)
        return EEPROM_FLASH_ERROR;
    return EEPROM_OK;
}

static void flash_lock_wrap(void)
{
    HAL_FLASH_Lock();
}

static EEPROM_Status erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,  /* 2.7 V – 3.6 V, word prog */
        .Sector       = sector,
        .NbSectors    = 1,
    };
    uint32_t sectorError = 0;
    if (HAL_FLASHEx_Erase(&erase, &sectorError) != HAL_OK)
        return EEPROM_FLASH_ERROR;
    return EEPROM_OK;
}

/** Write a single 32-bit word to flash (must be erased / 0xFFFFFFFF). */
static EEPROM_Status flash_write_word(uint32_t addr, uint32_t value)
{
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, value) != HAL_OK)
        return EEPROM_FLASH_ERROR;
    return EEPROM_OK;
}

/** Write an 8-byte record (header then data) into a page at a given index. */
static EEPROM_Status write_record(uint32_t page_base, uint32_t index,
                                   uint16_t vaddr, uint32_t data)
{
    uint32_t addr = page_base + RECORDS_START + index * RECORD_SIZE;
    EEPROM_Status s;

    /* Write data word first, then header.
     * On power-fail before header is written the record header stays 0xFFFFFFFF
     * (vaddr = 0xFFFF) and is treated as empty – safe. */
    s = flash_write_word(addr + DATA_OFFSET, data);
    if (s != EEPROM_OK) return s;

    s = flash_write_word(addr + HEADER_OFFSET, vaddr_to_header(vaddr));
    return s;
}

/* ─── Page management ────────────────────────────────────────────────────── */

/** Returns the base address of the currently VALID page, or 0 if none. */
static uint32_t active_page(void)
{
    if (page_status(EEPROM_PAGE0_BASE) == PAGE_STATUS_VALID)
        return EEPROM_PAGE0_BASE;
    if (page_status(EEPROM_PAGE1_BASE) == PAGE_STATUS_VALID)
        return EEPROM_PAGE1_BASE;
    return 0;
}

/** Returns the base address of the page that is NOT active. */
static uint32_t inactive_page(void)
{
    if (active_page() == EEPROM_PAGE0_BASE)
        return EEPROM_PAGE1_BASE;
    return EEPROM_PAGE0_BASE;
}

/** Returns the sector number for a given page base. */
static uint32_t sector_of(uint32_t base)
{
    return (base == EEPROM_PAGE0_BASE) ? EEPROM_PAGE0_SECTOR
                                       : EEPROM_PAGE1_SECTOR;
}

/**
 * Find the next free record slot index in @p page_base.
 * Returns MAX_RECORDS if the page is completely full.
 */
static uint32_t next_free_slot(uint32_t page_base)
{
    for (uint32_t i = 0; i < MAX_RECORDS; i++) {
        if (header_to_vaddr(record_header(page_base, i)) == 0xFFFFU)
            return i;
    }
    return MAX_RECORDS;
}

/**
 * Read the most-recent value for @p vaddr from @p page_base.
 * Scans backward to find the latest write.
 */
static EEPROM_Status read_from_page(uint32_t page_base, uint16_t vaddr,
                                    uint32_t *out)
{
    uint32_t last_slot = MAX_RECORDS; /* sentinel = not found */

    for (uint32_t i = 0; i < MAX_RECORDS; i++) {
        uint32_t hdr = record_header(page_base, i);
        if (header_to_vaddr(hdr) == 0xFFFFU)
            break;  /* hit empty slot – no more valid records */
        if (header_to_vaddr(hdr) == vaddr)
            last_slot = i;
    }

    if (last_slot == MAX_RECORDS)
        return EEPROM_NOT_FOUND;

    *out = record_data(page_base, last_slot);
    return EEPROM_OK;
}

/**
 * Page-swap / compaction.
 * Copies the latest value of every known variable from the active page to the
 * inactive (erased) page, then marks the new page VALID and erases the old one.
 *
 * @p new_vaddr / @p new_data  – if non-zero, also writes this new value into
 *                               the new page (used to merge a pending write).
 */
static EEPROM_Status page_swap(uint16_t new_vaddr, uint32_t new_data)
{
    EEPROM_Status s;
    uint32_t src  = active_page();
    uint32_t dst  = inactive_page();

    /* 1. Mark destination as RECEIVE */
    s = flash_write_word(dst, PAGE_STATUS_RECEIVE);
    if (s != EEPROM_OK) return s;

    /* 2. Copy latest value for each possible vaddr (1 .. EEPROM_MAX_VARS) */
    uint32_t dst_slot = 0;
    for (uint16_t va = 1; va <= EEPROM_MAX_VARS; va++) {
        /* Skip if this vaddr is the one being overwritten now */
        if (va == new_vaddr) continue;

        uint32_t val;
        if (read_from_page(src, va, &val) == EEPROM_OK) {
            s = write_record(dst, dst_slot, va, val);
            if (s != EEPROM_OK) return s;
            dst_slot++;
        }
    }

    /* 3. Write the new/updated value (if requested) */
    if (new_vaddr != 0) {
        s = write_record(dst, dst_slot, new_vaddr, new_data);
        if (s != EEPROM_OK) return s;
        dst_slot++;
    }

    /* 4. Mark destination as VALID */
    s = flash_write_word(dst, PAGE_STATUS_VALID);
    if (s != EEPROM_OK) return s;

    /* 5. Erase old page */
    s = erase_sector(sector_of(src));
    return s;
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

EEPROM_Status EEPROM_Init(void)
{
    EEPROM_Status s;
    uint32_t st0 = page_status(EEPROM_PAGE0_BASE);
    uint32_t st1 = page_status(EEPROM_PAGE1_BASE);

    s = flash_unlock_wrap();
    if (s != EEPROM_OK) return s;

    if (st0 == PAGE_STATUS_VALID && st1 == PAGE_STATUS_ERASED) {
        /* Normal: page 0 active */
        goto done;
    }

    if (st1 == PAGE_STATUS_VALID && st0 == PAGE_STATUS_ERASED) {
        /* Normal: page 1 active */
        goto done;
    }

    if (st0 == PAGE_STATUS_VALID && st1 == PAGE_STATUS_VALID) {
        /* Two valid pages – shouldn't happen; erase page 1 */
        s = erase_sector(EEPROM_PAGE1_SECTOR);
        goto done;
    }

    if (st0 == PAGE_STATUS_RECEIVE) {
        /* Power-failed during swap from page 1 → page 0.
         * Page 1 is still source.  Re-do the swap or just recover page 0. */
        if (st1 == PAGE_STATUS_VALID) {
            /* Page 1 was source, page 0 is partial destination.
             * Erase page 0 and re-trigger swap from page 1. */
            s = erase_sector(EEPROM_PAGE0_SECTOR);
            if (s != EEPROM_OK) goto done;
            s = page_swap(0, 0);
            goto done;
        }
        /* Both non-valid – format */
        s = EEPROM_Format();
        goto done;
    }

    if (st1 == PAGE_STATUS_RECEIVE) {
        if (st0 == PAGE_STATUS_VALID) {
            s = erase_sector(EEPROM_PAGE1_SECTOR);
            if (s != EEPROM_OK) goto done;
            s = page_swap(0, 0);
            goto done;
        }
        s = EEPROM_Format();
        goto done;
    }

    /* Neither page is valid – format */
    s = EEPROM_Format();

done:
    flash_lock_wrap();
    return s;
}

EEPROM_Status EEPROM_Read(uint16_t vaddr, uint32_t *data)
{
    if (vaddr == 0 || vaddr > EEPROM_MAX_VARS || data == NULL)
        return EEPROM_BAD_PARAM;

    uint32_t page = active_page();
    if (page == 0) return EEPROM_FLASH_ERROR;

    return read_from_page(page, vaddr, data);
}

EEPROM_Status EEPROM_Write(uint16_t vaddr, uint32_t data)
{
    if (vaddr == 0 || vaddr > EEPROM_MAX_VARS)
        return EEPROM_BAD_PARAM;

    EEPROM_Status s;
    uint32_t page = active_page();
    if (page == 0) return EEPROM_FLASH_ERROR;

    s = flash_unlock_wrap();
    if (s != EEPROM_OK) return s;

    uint32_t slot = next_free_slot(page);

    if (slot < MAX_RECORDS) {
        /* Space available – just append a new record */
        s = write_record(page, slot, vaddr, data);
    } else {
        /* Page full – compact and write new value in one step */
        s = page_swap(vaddr, data);
    }

    flash_lock_wrap();
    return s;
}

EEPROM_Status EEPROM_Format(void)
{
    EEPROM_Status s;

    s = flash_unlock_wrap();
    if (s != EEPROM_OK) return s;

    s = erase_sector(EEPROM_PAGE0_SECTOR);
    if (s != EEPROM_OK) { flash_lock_wrap(); return s; }

    s = erase_sector(EEPROM_PAGE1_SECTOR);
    if (s != EEPROM_OK) { flash_lock_wrap(); return s; }

    /* Mark page 0 as the initial active page */
    s = flash_write_word(EEPROM_PAGE0_BASE, PAGE_STATUS_VALID);

    flash_lock_wrap();
    return s;
}
