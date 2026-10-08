/*
 * boot_flash_ch32v30x.c
 *
 * CH32V30x internal-flash implementation of the boot_flash_port interface.
 *
 * Uses the WCH "fast" page mode (page = 256 bytes).  The flash controller must
 * not be clocked above its rating, so the core clock is halved for the duration
 * of every erase/program (RCC_HPRE_DIV2).
 */
#include "boot_flash_port.h"
#include "boot_protocol.h"

#include <string.h>

#include "debug.h"              /* ch32v30x.h -> RCC / FLASH / conf.h */
#include "ch32v30x_flash.h"

#define CH32_FLASH_PAGE_SIZE  (256U)

uint32_t boot_flash_app_start(void)
{
    return BOOT_APP_START;
}

uint32_t boot_flash_app_size(void)
{
    return BOOT_APP_SIZE;
}

uint32_t boot_flash_erase_size(void)
{
    return BOOT_DFU_SECTOR_SIZE;
}

bool boot_flash_addr_in_app(uint32_t addr, uint32_t len)
{
    if (addr < BOOT_APP_START) {
        return false;
    }
    if ((addr + len) > (BOOT_FLASH_BASE + BOOT_FLASH_SIZE)) {
        return false;
    }
    return true;
}

static void flash_begin(void)
{
    RCC->CFGR0 |= (uint32_t)RCC_HPRE_DIV2;
    FLASH_Unlock_Fast();
}

static void flash_end(void)
{
    RCC->CFGR0 &= ~(uint32_t)RCC_HPRE_DIV2;
}

int boot_flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t base;
    uint32_t end;

    if ((len == 0U) || !boot_flash_addr_in_app(addr, len)) {
        return -1;
    }

    base = addr & ~(BOOT_DFU_SECTOR_SIZE - 1U);
    end = (addr + len + BOOT_DFU_SECTOR_SIZE - 1U) & ~(BOOT_DFU_SECTOR_SIZE - 1U);

    flash_begin();
    for (uint32_t a = base; a < end; a += CH32_FLASH_PAGE_SIZE) {
        FLASH_ErasePage_Fast(a);
    }
    flash_end();

    return 0;
}

int boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint8_t page[CH32_FLASH_PAGE_SIZE];

    if ((len == 0U) || !boot_flash_addr_in_app(addr, len)) {
        return -1;
    }

    flash_begin();
    while (len > 0U) {
        uint32_t chunk = (len >= CH32_FLASH_PAGE_SIZE) ? CH32_FLASH_PAGE_SIZE : len;

        if (chunk < CH32_FLASH_PAGE_SIZE) {
            memset(page, 0xFF, CH32_FLASH_PAGE_SIZE);   /* pad the tail */
        }
        memcpy(page, data, chunk);
        FLASH_ProgramPage_Fast(addr, (uint32_t *)page);

        addr += CH32_FLASH_PAGE_SIZE;
        data += chunk;
        len -= chunk;
    }
    flash_end();

    return 0;
}

int boot_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if ((len == 0U) || !boot_flash_addr_in_app(addr, len)) {
        return -1;
    }
    memcpy(data, (const void *)addr, len);
    return 0;
}
