/*
 * ch32_dfu_desc.c
 *
 * USB DFU descriptors + init for the CH32V30x DFU bootloader.
 * Ported from hpm_dfu_boot/src/dfu_desc.c (DfuSe single-alt-setting, plus the
 * MS OS 1.0/WCID descriptors so Windows binds WinUSB to the DFU interface).
 */
#include "usbd_core.h"
#include "usbd_dfu.h"
#include "usb_dfu.h"

#include "boot_log.h"
#include "boot_protocol.h"
#include "boot_flash_port.h"
#include "usb_config.h"

#include <stdio.h>

/* DFU interface (9) + functional descriptor (9) */
#define DFU_DESC_TOTAL_LEN (9 + 9)
#define USB_CONFIG_SIZE    (9 + DFU_DESC_TOTAL_LEN)

/* Runtime-generated DfuSe memory layout string (iInterface = 4). */
static char flash_desc_str[64];

/* ------------------------------------------------------------------ *
 * Device / configuration descriptors
 * ------------------------------------------------------------------ */
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0200, 0x01)
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

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },   /* Langid 0x0409 */
    "CH32V30x",                     /* Manufacturer */
    "CH32V30x DFU Bootloader",      /* Product */
    "CH32DFU0001",                  /* Serial number */
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

#define MSOS_EXT_PROP_LEN (0x92U)
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
    static const char prop_name[] = "DeviceInterfaceGUIDs";
    static const char guid[] = DFU_INTERFACE_GUID;
    const uint32_t name_bytes = (uint32_t)sizeof(prop_name) * 2U;
    const uint32_t data_bytes = ((uint32_t)sizeof(guid) + 1U) * 2U;
    const uint32_t section_len = 4U + 4U + 2U + name_bytes + 4U + data_bytes;
    uint32_t p = 0U;
    uint32_t i;

    msos_ext_prop[p++] = (uint8_t)(10U + section_len);
    msos_ext_prop[p++] = (uint8_t)((10U + section_len) >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x01U;
    msos_ext_prop[p++] = 0x05U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x01U;
    msos_ext_prop[p++] = 0x00U;

    msos_ext_prop[p++] = (uint8_t)(section_len);
    msos_ext_prop[p++] = (uint8_t)(section_len >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x07U;   /* REG_MULTI_SZ */
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = (uint8_t)(name_bytes);
    msos_ext_prop[p++] = (uint8_t)(name_bytes >> 8);

    for (i = 0U; i < (uint32_t)sizeof(prop_name); i++) {
        msos_ext_prop[p++] = (uint8_t)prop_name[i];
        msos_ext_prop[p++] = 0x00U;
    }

    msos_ext_prop[p++] = (uint8_t)(data_bytes);
    msos_ext_prop[p++] = (uint8_t)(data_bytes >> 8);
    msos_ext_prop[p++] = 0x00U;
    msos_ext_prop[p++] = 0x00U;

    for (i = 0U; i < (uint32_t)sizeof(guid); i++) {
        msos_ext_prop[p++] = (uint8_t)guid[i];
        msos_ext_prop[p++] = 0x00U;
    }
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
    if (usbd_dfu_get_state() == DFU_STATE_DFU_MANIFEST_WAIT_RESET) {
        static uint8_t getstatus[6] = { DFU_STATUS_OK, 0, 0, 0,
                                        DFU_STATE_DFU_MANIFEST_WAIT_RESET, 0 };
        static uint8_t getstate = DFU_STATE_DFU_MANIFEST_WAIT_RESET;

        switch (setup->bRequest) {
            case DFU_REQUEST_GETSTATUS:
                *data = getstatus;
                *len = sizeof(getstatus);
                return 0;
            case DFU_REQUEST_GETSTATE:
                *data = &getstate;
                *len = 1;
                return 0;
            case DFU_REQUEST_DETACH:
                /* The host asked us to leave: main() resets shortly. */
                return 0;
            default:
                break;
        }
    }
    return dfu_class_handler(busid, setup, data, len);
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
    /* DfuSe memory layout: "@Internal Flash /0x<addr>/<n>*<size>Kg".
     * The sector size comes from the flash port's erase granularity. */
    uint32_t erase = boot_flash_erase_size();
    uint32_t sectors = boot_flash_app_size() / erase;

    (void)snprintf(flash_desc_str, sizeof(flash_desc_str),
                   "@Internal Flash /0x%08lX/%lu*%03luKg",
                   (unsigned long)boot_flash_app_start(),
                   (unsigned long)sectors,
                   (unsigned long)(erase / 1024U));

    msos_ext_prop_build();

    usbd_desc_register(busid, &dfu_descriptor);

    /* Install the CherryUSB DFU class handler, then wrap it (see above). */
    usbd_dfu_init_intf(&intf0);
    dfu_class_handler = intf0.class_interface_handler;
    intf0.class_interface_handler = dfu_intf_handler;
    usbd_add_interface(busid, &intf0);

    usbd_initialize(busid, reg_base, usbd_event_handler);
}
