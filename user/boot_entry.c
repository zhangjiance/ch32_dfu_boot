/*
 * boot_entry.c
 *
 * Boot entry decision, kept chip/board agnostic:
 *
 *   1. boot button held          -> stay in DFU
 *   2. DFU request from the APP  -> stay in DFU   (boot_trigger_port)
 *   3. valid application image   -> boot the APP
 *   otherwise                    -> stay in DFU
 *
 * Boards without a button simply never satisfy (1); DFU is then entered
 * through the application detach path.
 */
#include "boot_entry.h"

#include "boot_log.h"
#include "boot_protocol.h"
#include "board.h"
#include "boot_trigger_port.h"
#include "boot_usb_port.h"

#include "debug.h"      /* ch32v30x.h -> core_riscv (NVIC / Software_IRQn) */

/* ------------------------------------------------------------------ *
 * Application validity
 *
 * The application's vector table starts with the "j handle_reset" trampoline
 * emitted by the WCH startup code, i.e. a JAL with opcode 0x6F.  Erased flash
 * reads back as 0xFF, so one word tells a programmed image from a blank
 * partition.
 * ------------------------------------------------------------------ */
bool boot_app_is_valid(void)
{
    return (*(volatile uint32_t *)BOOT_APP_OFFSET & 0x7FU) == 0x6FU;
}

/* Runs in the software-interrupt context so the jump does not depend on the
 * caller's stack frame). */
void SW_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SW_Handler(void)
{
    __asm volatile("jr %0" : : "r"((uint32_t)BOOT_APP_OFFSET));
    while (1) {
    }
}

void boot_jump_to_app(void)
{
    BOOT_PRINTF("[BOOT] jumping to application @ 0x%08lX\r\n",
                (unsigned long)BOOT_APP_OFFSET);

    boot_usb_port_deinit();

    NVIC_EnableIRQ(Software_IRQn);
    NVIC_SetPendingIRQ(Software_IRQn);

    while (1) {
    }
}

void boot_check_and_run_app(void)
{
#if BOARD_HAS_BOOT_BUTTON
    if (board_read_boot_pin()) {
        BOOT_PRINTF("[BOOT] boot button pressed, staying in bootloader\r\n");
        /* Drop any stale request so the next reset boots the APP. */
        (void)boot_trigger_check_and_clear();
        return;
    }
#else
    BOOT_PRINTF("[BOOT] board has no boot button\r\n");
#endif

    if (boot_trigger_check_and_clear()) {
        BOOT_PRINTF("[BOOT] DFU request from application, staying in bootloader\r\n");
        return;
    }

    if (boot_app_is_valid()) {
        BOOT_PRINTF("[BOOT] valid application, booting\r\n");
        boot_jump_to_app();
    }

    BOOT_PRINTF("[BOOT] no valid application, staying in bootloader\r\n");
}
