/*
 * boot_entry.h
 *
 * Boot entry decision (chip/board agnostic):
 *   boot button / DFU request / application signature  ->  boot APP or stay.
 */
#ifndef BOOT_ENTRY_H
#define BOOT_ENTRY_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* true when a valid application image is present at BOOT_APP_OFFSET. */
bool boot_app_is_valid(void);

/* Leave the bootloader and hand over to the application.  Never returns. */
void boot_jump_to_app(void);

/* Run the boot decision: jumps to the application when it is valid and no
 * entry request is active.  Returns only when the bootloader must stay. */
void boot_check_and_run_app(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_ENTRY_H */
