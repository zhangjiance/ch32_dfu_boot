/*
 * usb_config.h
 *
 * CherryUSB (latest, port/wch/usbhs) configuration for the CH32V30x DFU
 * bootloader.  Device-only, DFU class, USBHS high speed.
 */
#ifndef USB_CONFIG_H
#define USB_CONFIG_H

#include "boot_log.h"

/* ================= common ================= */
#define CONFIG_USB_PRINTF(...) BOOT_PRINTF(__VA_ARGS__)
#define CONFIG_USB_DBG_LEVEL   USB_DBG_ERROR
#define CONFIG_USB_ALIGN_SIZE  4

/* CH32V30x has no data cache: DMA buffers can live in normal RAM. */
#define USB_NOCACHE_RAM_SECTION

/* ================= device ================= */
#define CONFIG_USB_DEVICE   1
#define CONFIG_USB_DEVICE_DFU 1

#define CONFIG_USBDEV_MAX_BUS             1
#define CONFIG_USBDEV_ADVANCE_DESC        1
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN  4096   /* >= wTransferSize */
#define CONFIG_USBDEV_EP_NUM              8

/* DFU transfer buffers */
#define CONFIG_USBDEV_DFU_XFER_SIZE       4096
#define CONFIG_USBDEV_DFU_MAX_BUFSIZE     4096

/* ================= descriptors ================= */
#define USBD_VID        0x1A86
#define USBD_PID        0xDF11
#define USBD_MAX_POWER  100

#endif /* USB_CONFIG_H */
