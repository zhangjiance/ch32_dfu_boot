/*
 * dfu_port.c
 *
 * DfuSe protocol -> boot_flash_port adapter (chip agnostic).
 *
 * The CherryUSB DFU class only forwards wValue (the DfuSe block index) to
 * usbd_dfu_write(); the DfuSe special commands therefore carry their target
 * address inside the payload:
 *     DNLOAD wValue=0 : data[0] = 0x21 (SET_ADDRESS) / 0x41 (ERASE),
 *                       data[1..4] = little-endian address
 *     DNLOAD wValue>=2: data block (wValue-2) * wTransferSize + base address
 */
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "usb_dfu.h"

#include "boot_log.h"
#include "boot_protocol.h"
#include "boot_flash_port.h"
#include "boot_board.h"

#include "dfu_port.h"

static uint32_t s_dfu_addr;
/*
 * Flash already erased in this DFU session, as a half-open window [base, end).
 *
 * It has to be a window rather than "the last sector erased": dfu-util first
 * erases every sector of the element and then, in its second pass, sends
 * SET_ADDRESS before each 4 KB chunk.  Remembering only the last sector meant
 * the first write of every chunk re-erased a sector that had just been erased,
 * doubling the erase time (and the flash wear).
 */
static uint32_t s_erased_base;
static uint32_t s_erased_end;
static volatile bool s_reboot_pending;

static bool range_is_erased(uint32_t addr, uint32_t len)
{
    return (s_erased_end > s_erased_base) &&
           (addr >= s_erased_base) && ((addr + len) <= s_erased_end);
}

static void note_range_erased(uint32_t base, uint32_t end)
{
    if (!(s_erased_end > s_erased_base) || (base > s_erased_end)) {
        /* Nothing erased yet, or a fresh range above the window: start over. */
        s_erased_base = base;
        s_erased_end = end;
        return;
    }
    if (base < s_erased_base) {
        s_erased_base = base;
    }
    if (end > s_erased_end) {
        s_erased_end = end;
    }
}

bool dfu_reboot_pending(void)
{
    return s_reboot_pending;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t erase_mask(void)
{
    return boot_flash_erase_size() - 1U;
}

/* ------------------------------------------------------------------ *
 * CherryUSB DFU class callbacks
 * ------------------------------------------------------------------ */

void usbd_dfu_begin_load(void)
{
    s_dfu_addr = boot_flash_app_start();
    s_erased_base = 0U;
    s_erased_end = 0U;
    BOOT_PRINTF("[DFU] begin load, base 0x%08lX\r\n", (unsigned long)s_dfu_addr);
}

void usbd_dfu_end_load(void)
{
    BOOT_PRINTF("[DFU] end load\r\n");
}

void usbd_dfu_reset(void)
{
    dfu_request_reboot();
}

void dfu_request_reboot(void)
{
    /* Manifestation finished: leave DFU mode by rebooting into the new image.
     * Flagged here (control-transfer context) and actioned from main(). */
    s_reboot_pending = true;
}

int usbd_dfu_write(uint16_t value, const uint8_t *data, uint16_t length)
{
    if (value == 0U) {
        uint32_t addr;

        if (length < 5U) {
            return 0;
        }
        addr = rd_le32(data + 1);

        if (data[0] == DFU_SPECIAL_CMD_SET_ADDRESS_POINTER) {
            /* Note this deliberately keeps s_erased_base/s_erased_end: the host
             * sends SET_ADDRESS again before every 4 KB chunk of the write pass,
             * and forgetting the erased window here made every chunk erase its
             * sector a second time. */
            s_dfu_addr = addr;
            return 0;
        }
        if (data[0] == DFU_SPECIAL_CMD_ERASE) {
            uint32_t sector = addr & ~erase_mask();

            if (boot_flash_erase(sector, boot_flash_erase_size()) != 0) {
                BOOT_PRINTF("[DFU] deny/erase fail 0x%08lX\r\n", (unsigned long)addr);
                return 1;
            }
            s_dfu_addr = addr;
            note_range_erased(sector, sector + boot_flash_erase_size());
            BOOT_PRINTF("[DFU] erase 0x%08lX\r\n", (unsigned long)addr);
            boot_board_led_toggle();
            return 0;
        }
        /* DFU_SPECIAL_CMD_READ_UNPROTECT and anything else: ignore */
        return 0;
    }

    if (value >= 2U) {
        uint32_t addr = s_dfu_addr + (uint32_t)(value - 2U) * BOOT_DFU_XFER_SIZE;
        uint32_t sector = addr & ~erase_mask();

        if (!boot_flash_addr_in_app(addr, length)) {
            BOOT_PRINTF("[DFU] deny write 0x%08lX len %u\r\n",
                        (unsigned long)addr, length);
            return 1;
        }

        /* Works with or without an explicit DfuSe ERASE: erase each sector the
         * first time a write lands in it (dfu-util programs sequentially).  When
         * the host already erased it in its erase pass this is skipped. */
        if (!range_is_erased(addr, length)) {
            if (boot_flash_erase(sector, boot_flash_erase_size()) != 0) {
                return 1;
            }
            note_range_erased(sector, sector + boot_flash_erase_size());
        }

        if (boot_flash_write(addr, data, length) != 0) {
            return 1;
        }
        boot_board_led_toggle();
        return 0;
    }

    return 0;
}

int usbd_dfu_read(uint16_t value, uint8_t *data, uint16_t length,
                  uint16_t *actual_length)
{
    *actual_length = 0;

    if (value == 0U) {
        if ((length >= 5U) && (data[0] == DFU_SPECIAL_CMD_SET_ADDRESS_POINTER)) {
            s_dfu_addr = rd_le32(data + 1);
        }
        return 0;
    }

    if (value >= 2U) {
        uint32_t addr = s_dfu_addr + (uint32_t)(value - 2U) * BOOT_DFU_XFER_SIZE;

        if (boot_flash_read(addr, data, length) != 0) {
            return -1;
        }
        *actual_length = length;
        return 0;
    }

    return -1;
}
