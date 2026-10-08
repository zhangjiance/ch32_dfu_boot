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

/* Arm that reboot.  Called from the DFU class glue when the host issues the
 * DfuSe "leave request" (a zero length DNLOAD) or DFU_DETACH.
 *
 * For a plain dfu-util session nothing else ever reaches usbd_dfu_reset() --
 * the CherryUSB class only calls it for a GETSTATUS in dfuMANIFEST_WAIT_RESET,
 * which DfuSe hosts never poll.  Without this the device would stay in
 * dfuMANIFEST_SYNC after the image is complete and the host would hang in its
 * leave hand-shake. */
void dfu_request_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* DFU_PORT_H */
