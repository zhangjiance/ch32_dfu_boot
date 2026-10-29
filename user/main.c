/*
 * main.c (bootloader)
 *
 * DFU bootloader entry point, chip and board agnostic.
 *
 * Power-on flow:
 *   1. board bring-up (boards/<board>/board.c + board_config.h)
 *   2. boot_check_and_run_app(): button / DFU request / APP signature
 *      -> never returns when the application is booted
 *   3. otherwise the chip port's USB device controller (USBHS on the CH32V30x,
 *      USBFS on the CH32L103) comes up as a DfuSe device and waits for dfu-util
 */
#include "ch32fun.h"

#include "board_config.h"
#include "board.h"
#include "boot_log.h"
#include "boot_entry.h"
#include "boot_protocol.h"
#include "boot_trigger_port.h"
#include "boot_usb_port.h"

#include "dfu_port.h"

extern void dfu_boot_init(uint8_t busid, uintptr_t reg_base);

/* Grace period between the end of a DFU download (manifestation) and the reset
 * that starts the new application.  Must outlast the host's leave hand-shake. */
#define DFU_LEAVE_DELAY_MS (500U)

/*
 * Put SYSCLK back on HSI before SystemInit() rebuilds the clock tree.  The
 * application leaves the chip with SYSCLK on the PLL, and ch32fun's SystemInit()
 * stops PLLON with its very first write, so without this the core would run out
 * of clock in the middle of SystemInit().  Safe on a power-on reset: SW already
 * selects HSI and the loops below exit at once.
 */
static void clock_fallback_to_hsi(void)
{
    RCC->CTLR |= RCC_HSION;
    while ((RCC->CTLR & RCC_HSIRDY) == 0U) {
    }

    RCC->CFGR0 &= ~RCC_SW;                          /* SW = HSI             */
    while ((RCC->CFGR0 & RCC_SWS) != 0U) {          /* wait for SWS = HSI   */
    }
}

int main(void)
{
    /*
     * Hand-over clock from the application.
     *
     * The application can be left either by a jump (bootloader -> app is the
     * other direction) or by a reset from the application, and ch32fun's
     * SystemInit() rebuilds the clock tree by *assigning* RCC->CTLR: its first
     * write clears PLLON.  If SYSCLK is still on the PLL at that point - the
     * state the application left the chip in, and the state a software reset
     * may not have cleared - that write stops the very clock the core is
     * running on and the chip dies on the first instruction of SystemInit().
     *
     * Switch back to HSI first (the same hand-over the application does before
     * its own SystemInit()); from a power-on reset this is a no-op.
     */
    clock_fallback_to_hsi();
    SystemInit();

    board_init();

    BOOT_PRINTF("\r\n\r\n");
    BOOT_PRINTF("========================================\r\n");
    BOOT_PRINTF("  %s DFU Bootloader (%s)\r\n", BOOT_CHIP_NAME, BOARD_NAME);
    BOOT_PRINTF("  Build: %s %s\r\n", __DATE__, __TIME__);
    BOOT_PRINTF("========================================\r\n\r\n");

    /* Jumps to the application if it is valid and no entry request is
     * active -- never returns in that case. */
    boot_check_and_run_app();

    BOOT_PRINTF("[BOOT] entering DFU mode...\r\n");

    /*
     * Hold D+ low for long enough that the host really processes a disconnect
     * before we attach again.
     *
     * A software reset (NVIC_SystemReset()) clears the controller registers,
     * which does drop the pull-up -- but only for as long as this boot takes.
     * When that window is too short Windows keeps a stale device instance: the
     * device still shows up in the device list, yet the first control transfer
     * after the (re)enumeration fails, and only the next one succeeds
     * (dfu-util reports "Failed to retrieve language identifiers" /
     * "Could not read name, sscanf returned 0").  The teardown holds D+ low for
     * ~15 ms; another 20 ms before the attach keeps the gap comfortably over
     * the 10 ms the host needs to notice, without slowing the re-enumeration
     * down.
     */
    boot_usb_port_deinit();
    board_delay_ms(20);

    dfu_boot_init(0, boot_usb_reg_base());

    while (1) {
        if (dfu_reboot_pending()) {
            /* Manifestation finished.  Wait long enough for the host to finish
             * its leave hand-shake, then run the freshly programmed
             * application -- resetting earlier makes the device vanish
             * mid-transaction and dfu-util reports
             * "Error during download get_status". */
            board_delay_ms(DFU_LEAVE_DELAY_MS);
            boot_system_reset();
        }
        board_delay_ms(1);
    }
}
