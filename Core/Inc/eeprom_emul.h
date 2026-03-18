/**
 * eeprom_emul.h - EEPROM Emulation for STM32F411
 *
 * Strategy: two Flash sectors used alternately for wear leveling.
 * Records are 32-bit: upper 16 bits = virtual address, lower 16 bits = data.
 * A page transfer (copy valid vars, erase old page) happens when active page fills up.
 *
 * Flash sectors used (STM32F411, 512KB device):
 *   Sector 6: 0x08040000  128KB  -> PAGE0
 *   Sector 7: 0x08060000  128KB  -> PAGE1
 *
 * Adjust PAGE0/PAGE1 addresses and sectors if you use a different memory map.
 */

#ifndef EEPROM_EMUL_H
#define EEPROM_EMUL_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ---------- Configuration ------------------------------------------- */
#define EEPROM_PAGE0_BASE_ADDR   0x08040000UL
#define EEPROM_PAGE1_BASE_ADDR   0x08060000UL
#define EEPROM_PAGE_SIZE         (128UL * 1024UL)

#define EEPROM_PAGE0_SECTOR      FLASH_SECTOR_6
#define EEPROM_PAGE1_SECTOR      FLASH_SECTOR_7

/* Voltage range for flash erase (match your VDD) */
#define EEPROM_VOLTAGE_RANGE     FLASH_VOLTAGE_RANGE_3  /* 2.7V – 3.6V */

/* Maximum number of distinct virtual addresses (keep well below page_size/4) */
#define EEPROM_MAX_VARIABLES     256U

/* ---------- Page status markers ------------------------------------- */
/* Written as a 32-bit word at byte offset 0 of each sector            */
#define PAGE_STATUS_ERASED       0xFFFFFFFFUL
#define PAGE_STATUS_RECEIVING    0xEEEEEEEEUL
#define PAGE_STATUS_ACTIVE       0x00000000UL

/* ---------- Return codes -------------------------------------------- */
#define EEPROM_OK                0x00U
#define EEPROM_NOT_FOUND         0x01U
#define EEPROM_NO_VALID_PAGE     0x02U
#define EEPROM_PAGE_FULL         0x03U
#define EEPROM_FLASH_ERROR       0x04U
#define EEPROM_BAD_ADDRESS       0x05U

/* ---------- API ------------------------------------------------------ */
uint16_t EEPROM_Init(void);
uint16_t EEPROM_ReadVariable(uint16_t virtAddr, uint16_t *pData);
uint16_t EEPROM_WriteVariable(uint16_t virtAddr, uint16_t data);
uint16_t EEPROM_Format(void);

#endif