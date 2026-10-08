/*
 * ch32_dfu_desc.c
 *
 * USB DFU descriptors + init for the CH32V30x DFU bootloader.
 *
 * DfuSe with a single alternate setting, plus MS OS 1.0/WCID descriptors so
 * Windows binds WinUSB to the DFU interface.
 */
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "usb_dfu.h"

#include "boot_log.h"
#include "boot_protocol.h"
#include "boot_flash_port.h"
#include "dfu_port.h"
#include "usb_config.h"

#include <stdio.h>

/* DFU interface (9) + functional descriptor (9) */
#define DFU_DESC_TOTAL_LEN (9 + 9)
#define USB_CONFIG_SIZE    (9 + DFU_DESC_TOTAL_LEN)

/* Runtime-generated DfuSe memory layout string (iInterface = 4). */
static char flash_desc_str[64];

/* Serial number from the CH32V30x unique id (0x1FFFF7E8), filled at init. */
static char serial_string[25];

/* ------------------------------------------------------------------ *
 * Device / configuration descriptors
 * ------------------------------------------------------------------ */
/* bcdDevice is bumped on every functional change: 'dfu-util -l' prints it as
 * ver=xxxx, which makes it obvious which firmware a board is running. */
#define USBD_BCD_DEVICE 0x0201

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, USBD_BCD_DEVICE, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    /* DFU interface 0, alt 0, DFU mode */
    0x09, 0x04, 0x00, 0x00, 0x00,
    0xFE, 0x01, 0x02,          /* APP_SPECIFIC / DFU / DFU_MODE */
    0x04,                      /* iInterface -> DfuSe layout string */
    /* DFU functional descriptor */
    0x09, 0x21,
    0x0B,                      /* CanDnload | CanUpload | WillDetach */
    0xFF, 0x00,                /* wDetachTimeout = 255 ms */
    0x00, 0x10,                /* wTransferSize = 4096 */
    0x1A, 0x01,                /* bcdDFU = 1.1a */
};

static const uint8_t device_quality_descriptor[] = {
    0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

static const uint8_t *device_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_cb(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static void build_serial_string(void)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *uid = (const uint8_t *)0x1FFFF7E8UL;
    uint32_t i;

    for (i = 0U; i < 12U; i++) {
        serial_string[i * 2U] = hex[(uid[i] >> 4) & 0x0FU];
        serial_string[(i * 2U) + 1U] = hex[uid[i] & 0x0FU];
    }
    serial_string[24] = '\0';
}

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },   /* Langid 0x0409 */
    "CH32V30x",                     /* Manufacturer */
    "CH32V30x DFU Bootloader",      /* Product */
    serial_string,                  /* Serial number */
    flash_desc_str,                 /* iInterface 4: DfuSe memory layout */
};

static const char *string_descriptor_cb(uint8_t speed, uint8_t index)
{
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

/* ------------------------------------------------------------------ *
 * MS OS 1.0 (WCID) -> Windows installs WinUSB for the DFU interface
 * ------------------------------------------------------------------ */
#define DFU_WINUSB_VENDOR_CODE 0x20U
#define DFU_INTERFACE_GUID     "{7a3e5c91-2b48-4d6f-a1e3-58c9d0b47f26}"

static const uint8_t msos_string[] = {
    0x12, 0x03,
    'M', 0x00, 'S', 0x00, 'F', 0x00, 'T', 0x00,
    '1', 0x00, '0', 0x00, '0', 0x00,
    DFU_WINUSB_VENDOR_CODE, 0x00,
};

static const uint8_t msos_compat_id[] = {
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,                   /* bFirstInterfaceNumber = 0 */
    0x01,
    0x57, 0x49, 0x4E, 0x55, /* "WINUSB\0\0" */
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/*
 * MS OS 1.0 "ExtProp" descriptor: a 10 byte header plus one feature-descriptor
 * section carrying the DeviceInterfaceGUIDs property.
 *
 * Windows reads it with a vendor request (bRequest = DFU_WINUSB_VENDOR_CODE,
 * wIndex = 5) right after the device is configured, and it takes the response
 * length from the first four bytes of this buffer.  A dwLength that does not
 * cover every byte written truncates the descriptor (Windows then drops the
 * property), so the layout is spelled out here and used by the builder below:
 *
 *   header (10) + section (4 + 4 + 2 + 40 + 4 + 80 = 134) = 144
 */
#define MSOS_PROP_NAME        "DeviceInterfaceGUIDs"
#define MSOS_PROP_NAME_BYTES  ((uint32_t)sizeof(MSOS_PROP_NAME) * 2U)
/* REG_MULTI_SZ payload: the GUID string, its own NUL, and the list's NUL. */
#define MSOS_PROP_DATA_BYTES  (((uint32_t)sizeof(DFU_INTERFACE_GUID) - 1U + 2U) * 2U)
#define MSOS_PROP_SECTION_LEN (4U + 4U + 2U + MSOS_PROP_NAME_BYTES + 4U + MSOS_PROP_DATA_BYTES)
#define MSOS_EXT_PROP_LEN     (10U + MSOS_PROP_SECTION_LEN)

static uint8_t msos_ext_prop[MSOS_EXT_PROP_LEN];

static const uint8_t msos_ext_prop_empty[] = {
    0x0a, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x05, 0x00,
    0x00, 0x00,
};

static const uint8_t *msos_ext_prop_list[2];

static const struct usb_msosv1_descriptor dfu_msosv1 = {
    .string = msos_string,
    .vendor_code = DFU_WINUSB_VENDOR_CODE,
    .compat_id = msos_compat_id,
    .comp_id_property = msos_ext_prop_list,
};

static void msos_ext_prop_build(void)
{
    static const char prop_name[] = MSOS_PROP_NAME;
    static const char guid[] = DFU_INTERFACE_GUID;
    uint32_t p = 0U;
    uint32_t i;

    /* dwLength / wVersion(0x0100) / wIndex(0x0005) / wCount(1) */
    msos_ext_prop[p++] = (uint8_t)(MSOS_EXT_PROP_LEN);
    msos_ext_prop[p++] = (uint8_t)(MSOS_EXT_PROP_LEN >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x01U;
    msos_ext_prop[p++] = 0x05U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x01U;
    msos_ext_prop[p++] = 0x00U;

    /* dwSize / dwPropertyDataType(REG_MULTI_SZ) / wPropertyNameLength */
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_SECTION_LEN);
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_SECTION_LEN >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x07U;   /* REG_MULTI_SZ */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_NAME_BYTES);
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_NAME_BYTES >> 8);

    /* bPropertyName, UTF-16LE (NUL included by sizeof) */
    for (i = 0U; i < (uint32_t)sizeof(prop_name); i++) {
        msos_ext_prop[p++] = (uint8_t)prop_name[i];
        msos_ext_prop[p++] = 0x00U;
    }

    /* dwPropertyDataLength */
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_DATA_BYTES);
    msos_ext_prop[p++] = (uint8_t)(MSOS_PROP_DATA_BYTES >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;

    /* bPropertyData, UTF-16LE GUID */
    for (i = 0U; i < (uint32_t)(sizeof(guid) - 1U); i++) {
        msos_ext_prop[p++] = (uint8_t)guid[i];
        msos_ext_prop[p++] = 0x00U;
    }
    /* REG_MULTI_SZ terminator: end of the string, then end of the list. */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;

    msos_ext_prop_list[0] = msos_ext_prop;
    msos_ext_prop_list[1] = msos_ext_prop_empty;
}

/* ------------------------------------------------------------------ */
const struct usb_descriptor dfu_descriptor = {
    .device_descriptor_callback         = device_descriptor_cb,
    .config_descriptor_callback         = config_descriptor_cb,
    .device_quality_descriptor_callback = device_quality_descriptor_cb,
    .other_speed_descriptor_callback    = config_descriptor_cb,
    .string_descriptor_callback         = string_descriptor_cb,
    .msosv1_descriptor                  = &dfu_msosv1,
    .msosv2_descriptor                  = NULL,
    .bos_descriptor                     = NULL,
};

static struct usbd_interface intf0;

/*
 * The CherryUSB DFU class handles every state except DFU_MANIFEST_WAIT_RESET:
 * once we report that state, the next class request hits its `default:` branch
 * and is STALLed.  dfu-util then fails its post-`:leave` get_status and prints
 *
 *     Submitting leave request...
 *     Error during download get_status
 *
 * even though the download already succeeded.  Wrap the class handler so the
 * shutdown hand-shake is answered cleanly; the switch to the application is
 * performed from main() shortly afterwards.
 */
static usbd_request_handler dfu_class_handler;

static int dfu_intf_handler(uint8_t busid, struct usb_setup_packet *setup,
                            uint8_t **data, uint32_t *len)
{
    int ret;

    if (usbd_dfu_get_state() == DFU_STATE_DFU_MANIFEST_WAIT_RESET) {
        static uint8_t getstatus[6] = { DFU_STATUS_OK, 0, 0, 0,
                                        DFU_STATE_DFU_MANIFEST_WAIT_RESET, 0 };
        static uint8_t getstate = DFU_STATE_DFU_MANIFEST_WAIT_RESET;

        switch (setup->bRequest) {
            case DFU_REQUEST_GETSTATUS:
                *data = getstatus;
                *len = sizeof(getstatus);
                /* Same effect the class would have had: leave DFU mode. */
                dfu_request_reboot();
                return 0;
            case DFU_REQUEST_GETSTATE:
                *data = &getstate;
                *len = 1;
                return 0;
            case DFU_REQUEST_DETACH:
                /* The host asked us to leave. */
                dfu_request_reboot();
                return 0;
            default:
                break;
        }
    }

    ret = dfu_class_handler(busid, setup, data, len);

    /*
     * DfuSe "leave request": a DNLOAD with no payload (dfuse.c sends it with
     * wValue = 2).  That is the only point at which dfu-util tells the device
     * the image is complete, and for a plain dfu-util session nothing else ever
     * calls usbd_dfu_reset() (the class does so only for a GETSTATUS in
     * dfuMANIFEST_WAIT_RESET, which DfuSe hosts never poll).  Without arming the
     * reboot here the device stays in dfuMANIFEST_SYNC with the image already
     * written, and dfu-util hangs on "Submitting leave request...".
     *
     * wValue != 0 keeps a stray zero-length DNLOAD from being mistaken for a
     * leave (the DFU spec numbers the manifest trigger after the last block).
     */
    if ((setup->bRequest == DFU_REQUEST_DNLOAD) && (setup->wLength == 0U) &&
        (setup->wValue != 0U)) {
        BOOT_PRINTF("[DFU] leave request, rebooting\r\n");
        dfu_request_reboot();
    }

    return ret;
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    (void)busid;
    switch (event) {
        case USBD_EVENT_RESET:      BOOT_PRINTF("[USB] RESET\r\n"); break;
        case USBD_EVENT_CONNECTED:  BOOT_PRINTF("[USB] CONNECTED\r\n"); break;
        case USBD_EVENT_CONFIGURED: BOOT_PRINTF("[USB] CONFIGURED\r\n"); break;
        default: break;
    }
}

void dfu_boot_init(uint8_t busid, uintptr_t reg_base)
{
    /* DfuSe memory layout: "@Internal Flash /0x<addr>/<n>*<size>K<type>".
     * The sector size comes from the flash port's erase granularity.
     *
     * The trailing type letter is a bit mask (dfu-util reads "memtype & 7";
     * 1 = readable, 2 = erasable, 4 = writable), so 'g' (0x67 & 7 = 7) means
     * readable + erasable + writable: the host erases every target page itself
     * before it starts downloading.  'M' (0x4D & 7 = 5) is the same except not
     * erasable, which makes the host skip that erase pass -- the firmware
     * erases a sector whenever a write lands in it (dfu_port.c) either way, so
     * the letter can be switched if the host-side erase pass is ever a problem.
     */
#define BOOT_DFUSE_SEGMENT_TYPE 'g'
    uint32_t erase = boot_flash_erase_size();
    uint32_t sectors = boot_flash_app_size() / erase;

    (void)snprintf(flash_desc_str, sizeof(flash_desc_str),
                   "@Internal Flash /0x%08lX/%lu*%03luK%c",
                   (unsigned long)boot_flash_app_start(),
                   (unsigned long)sectors,
                   (unsigned long)(erase / 1024U),
                   BOOT_DFUSE_SEGMENT_TYPE);

    build_serial_string();
    msos_ext_prop_build();

    usbd_desc_register(busid, &dfu_descriptor);

    /* Install the CherryUSB DFU class handler, then wrap it (see above). */
    usbd_dfu_init_intf(&intf0);
    dfu_class_handler = intf0.class_interface_handler;
    intf0.class_interface_handler = dfu_intf_handler;
    usbd_add_interface(busid, &intf0);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}
