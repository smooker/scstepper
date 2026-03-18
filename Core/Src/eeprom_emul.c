#include "eeprom_emul.h"
// #include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static uint32_t PageBaseAddr(uint8_t page)
{
    return (page == 0) ? EEPROM_PAGE0_BASE_ADDR : EEPROM_PAGE1_BASE_ADDR;
}

static uint8_t ActivePage(void)
{
    uint32_t s0 = *(volatile uint32_t *)EEPROM_PAGE0_BASE_ADDR;
    uint32_t s1 = *(volatile uint32_t *)EEPROM_PAGE1_BASE_ADDR;

    if (s0 == PAGE_STATUS_ACTIVE)  return 0;
    if (s1 == PAGE_STATUS_ACTIVE)  return 1;
    return 0xFF; /* no active page */
}

static HAL_StatusTypeDef EraseSector(uint32_t sector)
{
    FLASH_EraseInitTypeDef eraseInit = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Sector       = sector,
        .NbSectors    = 1,
        .VoltageRange = EEPROM_VOLTAGE_RANGE
    };
    uint32_t sectorError = 0;
    return HAL_FLASHEx_Erase(&eraseInit, &sectorError);
}

static HAL_StatusTypeDef WriteWord(uint32_t addr, uint32_t value)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint64_t)value);
}

/* Find the last (most recent) entry for virtAddr in the given page.
   Returns EEPROM_OK and fills *pData, or EEPROM_NOT_FOUND.           */
static uint16_t ReadFromPage(uint8_t page, uint16_t virtAddr, uint16_t *pData)
{
    uint32_t base  = PageBaseAddr(page);
    uint32_t end   = base + EEPROM_PAGE_SIZE;
    uint16_t found = EEPROM_NOT_FOUND;

    /* Records start at offset 4 (first word is page status) */
    for (uint32_t addr = base + 4; addr < end; addr += 4)
    {
        uint32_t word = *(volatile uint32_t *)addr;
        if (word == 0xFFFFFFFFUL) break; /* blank — end of written area */

        uint16_t va   = (uint16_t)(word >> 16);
        uint16_t data = (uint16_t)(word & 0xFFFFU);

        if (va == virtAddr)
        {
            *pData = data;
            found  = EEPROM_OK;
            /* keep scanning — we want the LAST written value */
        }
    }
    return found;
}

/* Find the next free slot in the active page. Returns 0 if page full.*/
static uint32_t NextFreeSlot(uint8_t page)
{
    uint32_t base = PageBaseAddr(page);
    uint32_t end  = base + EEPROM_PAGE_SIZE;

    for (uint32_t addr = base + 4; addr < end; addr += 4)
    {
        if (*(volatile uint32_t *)addr == 0xFFFFFFFFUL)
            return addr;
    }
    return 0; /* full */
}

/* Copy latest values of all known variables from srcPage to dstPage  */
static uint16_t PageTransfer(uint8_t srcPage, uint8_t dstPage)
{
    uint32_t dstBase    = PageBaseAddr(dstPage);
    // uint32_t dstSector  = (dstPage == 0) ? EEPROM_PAGE0_SECTOR : EEPROM_PAGE1_SECTOR;

    HAL_FLASH_Unlock();

    /* 1. Mark destination as "receiving" */
    if (WriteWord(dstBase, PAGE_STATUS_RECEIVING) != HAL_OK) goto err;

    /* 2. Copy latest value of each possible virtual address */
    uint32_t dstOffset = 4; /* first record slot */

    for (uint16_t va = 1; va <= EEPROM_MAX_VARIABLES; va++)
    {
        uint16_t data;
        if (ReadFromPage(srcPage, va, &data) == EEPROM_OK)
        {
            uint32_t dstAddr = dstBase + dstOffset;
            uint32_t record  = ((uint32_t)va << 16) | data;
            if (WriteWord(dstAddr, record) != HAL_OK) goto err;
            dstOffset += 4;
        }
    }

    /* 3. Mark destination as active */
    /* We can only flip bits 1->0 in flash; PAGE_STATUS_ACTIVE = 0x00000000 */
    if (WriteWord(dstBase, PAGE_STATUS_ACTIVE) != HAL_OK) goto err;

    /* 4. Erase source page */
    uint32_t srcSector = (srcPage == 0) ? EEPROM_PAGE0_SECTOR : EEPROM_PAGE1_SECTOR;
    if (EraseSector(srcSector) != HAL_OK) goto err;

    HAL_FLASH_Lock();
    return EEPROM_OK;

err:
    HAL_FLASH_Lock();
    return EEPROM_FLASH_ERROR;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

uint16_t EEPROM_Format(void)
{
    HAL_FLASH_Unlock();

    if (EraseSector(EEPROM_PAGE0_SECTOR) != HAL_OK) goto err;
    if (EraseSector(EEPROM_PAGE1_SECTOR) != HAL_OK) goto err;

    /* Mark page 0 as active */
    if (WriteWord(EEPROM_PAGE0_BASE_ADDR, PAGE_STATUS_ACTIVE) != HAL_OK) goto err;

    HAL_FLASH_Lock();
    return EEPROM_OK;

err:
    HAL_FLASH_Lock();
    return EEPROM_FLASH_ERROR;
}

uint16_t EEPROM_Init(void)
{
    uint32_t s0 = *(volatile uint32_t *)EEPROM_PAGE0_BASE_ADDR;
    uint32_t s1 = *(volatile uint32_t *)EEPROM_PAGE1_BASE_ADDR;

    if (s0 == PAGE_STATUS_ACTIVE && s1 == PAGE_STATUS_ERASED)
        return EEPROM_OK; /* normal state */

    if (s1 == PAGE_STATUS_ACTIVE && s0 == PAGE_STATUS_ERASED)
        return EEPROM_OK; /* page 1 is active after a transfer */

    /* Interrupted transfer: page 0 active, page 1 receiving -> redo transfer */
    if (s0 == PAGE_STATUS_ACTIVE && s1 == PAGE_STATUS_RECEIVING)
        return PageTransfer(0, 1);

    if (s1 == PAGE_STATUS_ACTIVE && s0 == PAGE_STATUS_RECEIVING)
        return PageTransfer(1, 0);

    /* Both pages erased or corrupt: format */
    // BKPT;           //fixme smooker
    return EEPROM_Format();
}

uint16_t EEPROM_ReadVariable(uint16_t virtAddr, uint16_t *pData)
{
    if (virtAddr == 0 || virtAddr > EEPROM_MAX_VARIABLES)
        return EEPROM_BAD_ADDRESS;

    uint8_t page = ActivePage();
    if (page == 0xFF) return EEPROM_NO_VALID_PAGE;

    return ReadFromPage(page, virtAddr, pData);
}

uint16_t EEPROM_WriteVariable(uint16_t virtAddr, uint16_t data)
{
    if (virtAddr == 0 || virtAddr > EEPROM_MAX_VARIABLES)
        return EEPROM_BAD_ADDRESS;

    uint8_t page = ActivePage();
    if (page == 0xFF) return EEPROM_NO_VALID_PAGE;

    uint32_t slot = NextFreeSlot(page);

    if (slot == 0)
    {
        /* Page full — transfer to the other page then write */
        uint8_t newPage = (page == 0) ? 1 : 0;
        uint32_t newBase = PageBaseAddr(newPage);
        uint32_t newSector = (newPage == 0) ? EEPROM_PAGE0_SECTOR : EEPROM_PAGE1_SECTOR;

        HAL_FLASH_Unlock();

        /* Erase destination if not already blank */
        if (*(volatile uint32_t *)newBase != PAGE_STATUS_ERASED)
        {
            if (EraseSector(newSector) != HAL_OK)
            {
                HAL_FLASH_Lock();
                return EEPROM_FLASH_ERROR;
            }
        }
        HAL_FLASH_Lock();

        if (PageTransfer(page, newPage) != EEPROM_OK)
            return EEPROM_FLASH_ERROR;

        page = newPage;
        slot = NextFreeSlot(page);
        if (slot == 0) return EEPROM_PAGE_FULL; /* should never happen */
    }

    HAL_FLASH_Unlock();
    uint32_t record = ((uint32_t)virtAddr << 16) | data;
    HAL_StatusTypeDef st = WriteWord(slot, record);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? EEPROM_OK : EEPROM_FLASH_ERROR;
}
