/*
 * main.c (bootloader)
 *
 * CH32V30x DFU bootloader entry point.
 *
 * Power-on flow:
 *   1. board bring-up (boards/<board>/board.c + board_config.h)
 *   2. boot_check_and_run_app(): button / DFU request / APP signature
 *      -> never returns when the application is booted
 *   3. otherwise USBHS comes up as a DfuSe device and waits for dfu-util
 */
#include "debug.h"

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

int main(void)
{
    board_init();
    /* The log UART is an application concern - the board layer owns pins, not
     * a console - so it is brought up here, and only in the logging build. */
#if BOOT_LOG_ENABLED
    USART_Printf_Init(BOOT_LOG_BAUDRATE);
#endif

    BOOT_PRINTF("\r\n\r\n");
    BOOT_PRINTF("========================================\r\n");
    BOOT_PRINTF("  CH32V30x DFU Bootloader (%s)\r\n", BOARD_NAME);
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
     * A software reset (NVIC_SystemReset()) clears the USBHS registers, which
     * does drop the pull-up -- but only for as long as this boot takes.  When
     * that window is too short Windows keeps a stale device instance: the
     * device still shows up in the device list, yet the first control transfer
     * after the (re)enumeration fails, and only the next one succeeds
     * (dfu-util reports "Failed to retrieve language identifiers" /
     * "Could not read name, sscanf returned 0").  Settling for >10 ms before the
     * re-enumeration avoids it.
     */
    boot_usb_port_deinit();
    board_delay_ms(50);

    dfu_boot_init(0, USBHS_BASE);

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
