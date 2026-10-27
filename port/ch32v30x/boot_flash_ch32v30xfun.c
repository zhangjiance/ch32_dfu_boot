/*
 * boot_flash_ch32v30xfun.c
 *
 * CH32V30x internal-flash implementation of the boot_flash_port interface,
 * written against the peripheral registers ch32fun declares (no peripheral
 * library).  Same algorithm as boot_flash_ch32v30x.c:
 *
 *   - the WCH "fast" mode is used, page = 256 bytes
 *   - the flash controller must not be clocked above its rating, so HCLK is
 *     halved for the duration of every erase/program (RCC_HPRE_DIV2)
 *
 * The erase/program sequences are the ones the WCH library performs
 * (FLASH_Unlock_Fast / FLASH_ErasePage_Fast / FLASH_ProgramPage_Fast),
 * inlined so the HAL does not have to be linked in.
 */
#include "boot_flash_port.h"
#include "boot_protocol.h"

#include <string.h>

#include "ch32fun.h"

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

static void flash_wait_idle(void)
{
    while (FLASH->STATR & SR_BSY) {
    }
    while (FLASH->STATR & SR_WR_BSY) {
    }
}

static void flash_begin(void)
{
    RCC->CFGR0 |= (uint32_t)RCC_HPRE_DIV2;

    /* Authorise the FPEC of bank 1 and unlock the fast program mode. */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;
    FLASH->MODEKEYR = FLASH_KEY2;
}

static void flash_end(void)
{
    RCC->CFGR0 &= ~(uint32_t)RCC_HPRE_DIV2;
}

static void flash_erase_page(uint32_t addr)
{
    FLASH->CTLR |= CR_PAGE_ER;
    FLASH->ADDR = addr & ~0xFFU;
    FLASH->CTLR |= CR_STRT_Set;
    while (FLASH->STATR & SR_BSY) {
    }
    FLASH->CTLR &= ~CR_PAGE_ER;
}

static void flash_program_page(uint32_t addr, const uint32_t *page)
{
    uint32_t size = CH32_FLASH_PAGE_SIZE / 4U;

    addr &= ~0xFFU;

    FLASH->CTLR |= CR_PAGE_PG;
    flash_wait_idle();

    while (size--) {
        *(volatile uint32_t *)addr = *page++;
        addr += 4U;
        while (FLASH->STATR & SR_WR_BSY) {
        }
    }

    FLASH->CTLR |= CR_PG_STRT;
    while (FLASH->STATR & SR_BSY) {
    }
    FLASH->CTLR &= ~CR_PAGE_PG;
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
        flash_erase_page(a);
    }
    flash_end();

    return 0;
}

int boot_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint8_t page[CH32_FLASH_PAGE_SIZE] __attribute__((aligned(4)));

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
        flash_program_page(addr, (const uint32_t *)page);

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
