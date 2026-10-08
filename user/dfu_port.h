/*
 * dfu_port.h
 *
 * DfuSe protocol -> boot_flash_port adapter.  Chip agnostic: all flash
 * access goes through the abstract boot_flash_port interface.
 */
#ifndef DFU_PORT_H
#define DFU_PORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set once the host has finished a download and the device should reboot into
 * the freshly programmed application.  Polled from main(). */
bool dfu_reboot_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* DFU_PORT_H */
