/*
 * boot_flash_ch32l103.c
 *
 * CH32L103 internal-flash implementation of the boot_flash_port interface,
 * written against the peripheral registers ch32fun declares (ch32l103hw.h).
 *
 * Sequence references: the CH32L103 EVT flash library
 * (CH32L103EVT/EVT/EXAM/SRC/Peripheral/src/ch32l103_flash.c, functions
 * FLASH_ROM_ERASE / FLASH_ROM_WRITE, "recommended for flash erasing/programming"
 * by WCH).  Compared to the CH32V30x port, two things differ in a way that
 * matters:
 *
 *   - the fast program path goes through a 256 byte page buffer: each of the 64
 *     words is stored to its target address and then latched with BUFLOAD, and
 *     ADDR + STRT are only written after the last word to commit the page.
 *     Storing the words straight into flash and starting afterwards, as the
 *     V30x does, leaves the buffer unloaded and programs nothing.
 *   - HCLK is not divided for erase/program: the SDK example does the whole
 *     sequence at the full 96 MHz.
 *
 * Fast mode means page = 256 bytes (FLASH_CTLR_FTER / FLASH_CTLR_FTPG); the
 * standard mode page is 2 KB (CR_PER) and there are 32 KB block erases, neither
 * of which a 4 KB DfuSe sector needs.  Beware of the generic CR_* aliases in
 * ch32l103hw.h: they are carried over from the CH32V30x header, and on the L103
 * CR_PG_STRT (bit 21) and CR_BER32/CR_BER64 land on reserved bits - the SDK
 * starts the page program with the plain STRT bit and puts BER32 on bit 23.
 * Hence the explicit FLASH_CTLR_* names below.
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
}

static void flash_begin(void)
{
    /* Authorise the FPEC of bank 1 (FLASH_Unlock_Fast in the WCH library). */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;
    FLASH->MODEKEYR = FLASH_KEY2;
    flash_wait_idle();
}

static void flash_end(void)
{
    /* Re-lock both the standard and the fast interface. */
    FLASH->CTLR |= FLASH_CTLR_LOCK | FLASH_CTLR_FLOCK;
    flash_wait_idle();
}

/* The WCH library starts every operation with
 *     FLASH->CTLR &= (CR_OPTER_Reset & CR_PAGE_ER_Reset);
 * i.e. it drops the option-byte and page-erase request bits, so a stray one
 * cannot leak into the next command. */
static void flash_clear_requests(void)
{
    FLASH->CTLR &= ~(FLASH_CTLR_OPTER | FLASH_CTLR_FTER);
}

/* One 256 byte fast-mode page (ROM_ERASE(..., Size_256B)). */
static void flash_erase_page(uint32_t addr)
{
    flash_clear_requests();
    FLASH->CTLR |= FLASH_CTLR_FTER;
    FLASH->ADDR = addr & ~0xFFU;
    FLASH->CTLR |= CR_STRT_Set;
    flash_wait_idle();
    FLASH->CTLR &= ~FLASH_CTLR_FTER;
}

/*
 * One 256 byte fast-mode page; [page] holds CH32_FLASH_PAGE_SIZE bytes.
 *
 * This is FLASH_ROM_WRITE() of the CH32L103 SDK, which is the sequence to
 * follow on this part: the 64 words are pushed into the controller's page
 * buffer (store to the target address, then BUFLOAD, then wait) while FTPG is
 * held, and ADDR/STRT are only written afterwards to commit the page.  BUFRST
 * and BUFLOAD are pulses - the library never clears them.
 */
static void flash_program_page(uint32_t addr, const uint32_t *page)
{
    uint32_t dst = addr & ~0xFFU;

    flash_clear_requests();

    FLASH->CTLR |= FLASH_CTLR_FTPG;
    FLASH->CTLR |= FLASH_CTLR_BUFRST;       /* start from an empty buffer */
    flash_wait_idle();

    for (uint32_t i = 0; i < (CH32_FLASH_PAGE_SIZE / 4U); i++) {
        *(volatile uint32_t *)dst = page[i];    /* data goes to its address */
        FLASH->CTLR |= FLASH_CTLR_BUFLOAD;      /* ... and is latched into BUF */
        flash_wait_idle();
        dst += 4U;
    }

    FLASH->ADDR = addr & ~0xFFU;
    FLASH->CTLR |= CR_STRT_Set;             /* program the whole page */
    flash_wait_idle();
    FLASH->CTLR &= ~FLASH_CTLR_FTPG;
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
