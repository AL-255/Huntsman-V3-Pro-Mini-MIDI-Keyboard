#include "usb_descriptors.h"

#include <stddef.h>

#include "usb_device_cdc_acm.h"
#include "usb_device_hid.h"
#include "usb_spec.h"

#define USB_U16_LO(x) ((uint8_t)((x) & 0xffu))
#define USB_U16_HI(x) ((uint8_t)(((x) >> 8u) & 0xffu))

#define USB_VENDOR_ID  0x1532u
#define USB_PRODUCT_ID 0x02b0u

static uint8_t s_device_descriptor[] = {
    18u, USB_DESCRIPTOR_TYPE_DEVICE,
    0x00u, 0x02u,
    0xefu, 0x02u, 0x01u,
    64u,
    USB_U16_LO(USB_VENDOR_ID), USB_U16_HI(USB_VENDOR_ID),
    USB_U16_LO(USB_PRODUCT_ID), USB_U16_HI(USB_PRODUCT_ID),
    0x01u, 0x02u,
    1u, 2u, 3u,
    1u,
};

static uint8_t s_device_qualifier[] = {
    10u, USB_DESCRIPTOR_TYPE_DEVICE_QUALITIER,
    0x00u, 0x02u, 0xefu, 0x02u, 0x01u, 64u, 1u, 0u,
};

static const uint8_t s_keyboard_report_descriptor[] = {
    0x05u, 0x01u,       /* Usage Page (Generic Desktop) */
    0x09u, 0x06u,       /* Usage (Keyboard) */
    0xa1u, 0x01u,       /* Collection (Application) */
    0x05u, 0x07u,       /* Usage Page (Keyboard) */
    0x19u, 0xe0u,       /* Usage Minimum (Left Control) */
    0x29u, 0xe7u,       /* Usage Maximum (Right GUI) */
    0x15u, 0x00u, 0x25u, 0x01u,
    0x75u, 0x01u, 0x95u, 0x08u,
    0x81u, 0x02u,       /* Input (Data, Variable, Absolute) */
    0x75u, 0x08u, 0x95u, 0x01u,
    0x81u, 0x01u,       /* Reserved byte */
    0x19u, 0x04u, 0x29u, 0x73u,
    0x15u, 0x00u, 0x25u, 0x01u,
    0x75u, 0x01u, 0x95u, 0x70u,
    0x81u, 0x02u,       /* 112-key NKRO bitmap */
    0xc0u,
};

static const uint8_t s_updater_report_descriptor[] = {
    0x06u, 0x00u, 0xffu, /* Usage Page (Vendor Defined) */
    0x09u, 0x01u,
    0xa1u, 0x01u,
    0x15u, 0x00u, 0x26u, 0xffu, 0x00u,
    0x75u, 0x08u, 0x95u, 0x5au,
    0x09u, 0x01u, 0xb1u, 0x02u, /* 90-byte Feature, no report ID */
    0xc0u,
};

/* Descriptor offsets used by usb_descriptors_set_speed(). */
enum
{
    CFG_KEYBOARD_HID_OFFSET = 18,
    CFG_UPDATER_HID_OFFSET = 135,
};

static uint8_t s_configuration_descriptor[] = {
    /* Configuration, six interfaces. Total length 210 bytes. */
    9u, USB_DESCRIPTOR_TYPE_CONFIGURE, 0xd2u, 0x00u, USB_IFACE_COUNT, 1u, 0u, 0xa0u, 250u,

    /* Interface 0: HID NKRO keyboard. */
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_KEYBOARD, 0u, 1u, 0x03u, 0x01u, 0x01u, 4u,
    9u, USB_DESCRIPTOR_TYPE_HID, 0x11u, 0x01u, 0u, 1u, USB_DESCRIPTOR_TYPE_HID_REPORT,
    USB_U16_LO(sizeof(s_keyboard_report_descriptor)), USB_U16_HI(sizeof(s_keyboard_report_descriptor)),
    7u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_IN | USB_KEYBOARD_ENDPOINT, 0x03u,
    USB_U16_LO(USB_KEYBOARD_PACKET), USB_U16_HI(USB_KEYBOARD_PACKET), 1u,

    /* Interface 1: USB Audio 1.0 control header for MIDIStreaming interface 2. */
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_MIDI_CONTROL, 0u, 0u, 0x01u, 0x01u, 0x00u, 5u,
    9u, 0x24u, 0x01u, 0x00u, 0x01u, 0x09u, 0x00u, 0x01u, USB_IFACE_MIDI_STREAM,

    /* Interface 2: USB-MIDI 1.0, one virtual cable in each direction. */
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_MIDI_STREAM, 0u, 2u, 0x01u, 0x03u, 0x00u, 5u,
    7u, 0x24u, 0x01u, 0x00u, 0x01u, 0x41u, 0x00u,
    6u, 0x24u, 0x02u, 0x01u, 0x01u, 0u, /* Embedded MIDI IN jack 1 */
    6u, 0x24u, 0x02u, 0x02u, 0x02u, 0u, /* External MIDI IN jack 2 */
    9u, 0x24u, 0x03u, 0x01u, 0x03u, 1u, 0x02u, 1u, 0u, /* Embedded OUT jack 3 <- 2 */
    9u, 0x24u, 0x03u, 0x02u, 0x04u, 1u, 0x01u, 1u, 0u, /* External OUT jack 4 <- 1 */
    9u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_OUT | USB_MIDI_ENDPOINT, 0x02u,
    USB_U16_LO(USB_FS_BULK_PACKET), USB_U16_HI(USB_FS_BULK_PACKET), 0u, 0u, 0u,
    5u, 0x25u, 0x01u, 1u, 0x01u,
    9u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_IN | USB_MIDI_ENDPOINT, 0x02u,
    USB_U16_LO(USB_FS_BULK_PACKET), USB_U16_HI(USB_FS_BULK_PACKET), 0u, 0u, 0u,
    5u, 0x25u, 0x01u, 1u, 0x03u,

    /* Interface 3: factory-compatible 90-byte feature-report transport. */
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_UPDATER, 0u, 0u, 0x03u, 0x00u, 0x00u, 6u,
    9u, USB_DESCRIPTOR_TYPE_HID, 0x11u, 0x01u, 0u, 1u, USB_DESCRIPTOR_TYPE_HID_REPORT,
    USB_U16_LO(sizeof(s_updater_report_descriptor)), USB_U16_HI(sizeof(s_updater_report_descriptor)),

    /* CDC ACM IAD, control interface 4, data interface 5. */
    8u, USB_DESCRIPTOR_TYPE_INTERFACE_ASSOCIATION, USB_IFACE_CDC_CONTROL, 2u, 0x02u, 0x02u, 0x01u, 7u,
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_CDC_CONTROL, 0u, 1u, 0x02u, 0x02u, 0x01u, 7u,
    5u, 0x24u, 0x00u, 0x10u, 0x01u,
    5u, 0x24u, 0x01u, 0x00u, USB_IFACE_CDC_DATA,
    4u, 0x24u, 0x02u, 0x02u,
    5u, 0x24u, 0x06u, USB_IFACE_CDC_CONTROL, USB_IFACE_CDC_DATA,
    7u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_IN | USB_CDC_NOTIFICATION_ENDPOINT, 0x03u,
    USB_U16_LO(USB_CDC_NOTIFICATION_PACKET), USB_U16_HI(USB_CDC_NOTIFICATION_PACKET), 8u,
    9u, USB_DESCRIPTOR_TYPE_INTERFACE, USB_IFACE_CDC_DATA, 0u, 2u, 0x0au, 0x00u, 0x00u, 7u,
    7u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_OUT | USB_CDC_DATA_ENDPOINT, 0x02u,
    USB_U16_LO(USB_FS_BULK_PACKET), USB_U16_HI(USB_FS_BULK_PACKET), 0u,
    7u, USB_DESCRIPTOR_TYPE_ENDPOINT, USB_ENDPOINT_IN | USB_CDC_DATA_ENDPOINT, 0x02u,
    USB_U16_LO(USB_FS_BULK_PACKET), USB_U16_HI(USB_FS_BULK_PACKET), 0u,
};

_Static_assert(sizeof(s_configuration_descriptor) == 210u, "USB configuration length mismatch");

static uint8_t s_string0[] = {4u, USB_DESCRIPTOR_TYPE_STRING, 0x09u, 0x04u};
static uint8_t s_string1[] = {26u, USB_DESCRIPTOR_TYPE_STRING,
    'O',0,'p',0,'e',0,'n',0,'H',0,'u',0,'n',0,'t',0,'s',0,'m',0,'a',0,'n',0};
static uint8_t s_string2[] = {52u, USB_DESCRIPTOR_TYPE_STRING,
    'H',0,'u',0,'n',0,'t',0,'s',0,'m',0,'a',0,'n',0,' ',0,'V',0,'3',0,' ',0,'P',0,'r',0,'o',0,
    ' ',0,'M',0,'i',0,'n',0,'i',0,' ',0,'M',0,'I',0,'D',0,'I',0};
static uint8_t s_string3[] = {42u, USB_DESCRIPTOR_TYPE_STRING,
    'O',0,'P',0,'E',0,'N',0,'H',0,'U',0,'N',0,'T',0,'S',0,'M',0,'A',0,'N',0,'0',0,'0',0,'0',0,
    '0',0,'0',0,'0',0,'0',0,'0',0};
static uint8_t s_string4[] = {28u, USB_DESCRIPTOR_TYPE_STRING,
    'N',0,'K',0,'R',0,'O',0,' ',0,'K',0,'e',0,'y',0,'b',0,'o',0,'a',0,'r',0,'d',0};
static uint8_t s_string5[] = {18u, USB_DESCRIPTOR_TYPE_STRING,
    'M',0,'I',0,'D',0,'I',0,' ',0,'1',0,'.',0,'0',0};
static uint8_t s_string6[] = {32u, USB_DESCRIPTOR_TYPE_STRING,
    'F',0,'i',0,'r',0,'m',0,'w',0,'a',0,'r',0,'e',0,' ',0,'C',0,'o',0,'n',0,'f',0,'i',0,'g',0};
static uint8_t s_string7[] = {20u, USB_DESCRIPTOR_TYPE_STRING,
    'D',0,'e',0,'b',0,'u',0,'g',0,' ',0,'C',0,'D',0,'C',0};

static uint8_t *const s_strings[] = {
    s_string0, s_string1, s_string2, s_string3, s_string4, s_string5, s_string6, s_string7,
};

usb_device_endpoint_struct_t g_keyboardEndpoints[] = {
    {USB_ENDPOINT_IN | USB_KEYBOARD_ENDPOINT, USB_ENDPOINT_INTERRUPT, USB_KEYBOARD_PACKET, 1u},
};
static usb_device_interface_struct_t s_keyboardInterfacesAlt[] = {
    {0u, {1u, g_keyboardEndpoints}, NULL},
};
static usb_device_interfaces_struct_t s_keyboardInterfaces[] = {
    {0x03u, 0x01u, 0x01u, USB_IFACE_KEYBOARD, s_keyboardInterfacesAlt, 1u},
};
static usb_device_interface_list_t s_keyboardInterfaceList[] = {
    {1u, s_keyboardInterfaces},
};
usb_device_class_struct_t g_keyboardClass = {s_keyboardInterfaceList, kUSB_DeviceClassTypeHid, 1u};

usb_device_endpoint_struct_t g_midiEndpoints[] = {
    {USB_ENDPOINT_OUT | USB_MIDI_ENDPOINT, USB_ENDPOINT_BULK, USB_FS_BULK_PACKET, 0u},
    {USB_ENDPOINT_IN | USB_MIDI_ENDPOINT, USB_ENDPOINT_BULK, USB_FS_BULK_PACKET, 0u},
};
static usb_device_interface_struct_t s_updaterAlt[] = {
    {0u, {0u, NULL}, NULL},
};
static usb_device_interfaces_struct_t s_updaterInterfaces[] = {
    {0x03u, 0x00u, 0x00u, USB_IFACE_UPDATER, s_updaterAlt, 1u},
};
static usb_device_interface_list_t s_updaterInterfaceList[] = {
    {1u, s_updaterInterfaces},
};
usb_device_class_struct_t g_updaterClass = {s_updaterInterfaceList, kUSB_DeviceClassTypeHid, 1u};

usb_device_endpoint_struct_t g_cdcControlEndpoints[] = {
    {USB_ENDPOINT_IN | USB_CDC_NOTIFICATION_ENDPOINT, USB_ENDPOINT_INTERRUPT, USB_CDC_NOTIFICATION_PACKET, 8u},
};
usb_device_endpoint_struct_t g_cdcDataEndpoints[] = {
    {USB_ENDPOINT_IN | USB_CDC_DATA_ENDPOINT, USB_ENDPOINT_BULK, USB_FS_BULK_PACKET, 0u},
    {USB_ENDPOINT_OUT | USB_CDC_DATA_ENDPOINT, USB_ENDPOINT_BULK, USB_FS_BULK_PACKET, 0u},
};
static usb_device_interface_struct_t s_cdcControlAlt[] = {
    {0u, {1u, g_cdcControlEndpoints}, NULL},
};
static usb_device_interface_struct_t s_cdcDataAlt[] = {
    {0u, {2u, g_cdcDataEndpoints}, NULL},
};
static usb_device_interfaces_struct_t s_cdcInterfaces[] = {
    {0x02u, 0x02u, 0x01u, USB_IFACE_CDC_CONTROL, s_cdcControlAlt, 1u},
    {0x0au, 0x00u, 0x00u, USB_IFACE_CDC_DATA, s_cdcDataAlt, 1u},
};
static usb_device_interface_list_t s_cdcInterfaceList[] = {
    {2u, s_cdcInterfaces},
};
usb_device_class_struct_t g_cdcClass = {s_cdcInterfaceList, kUSB_DeviceClassTypeCdc, 1u};

static void patch_endpoint_packet_sizes(uint16_t bulk_size)
{
    for (size_t offset = 0u; offset + 7u <= sizeof(s_configuration_descriptor);)
    {
        const uint8_t length = s_configuration_descriptor[offset];
        if (length == 0u)
        {
            break;
        }
        if ((s_configuration_descriptor[offset + 1u] == USB_DESCRIPTOR_TYPE_ENDPOINT) &&
            ((s_configuration_descriptor[offset + 3u] & 0x03u) == USB_ENDPOINT_BULK))
        {
            s_configuration_descriptor[offset + 4u] = USB_U16_LO(bulk_size);
            s_configuration_descriptor[offset + 5u] = USB_U16_HI(bulk_size);
        }
        offset += length;
    }
}

void usb_descriptors_set_speed(uint8_t speed)
{
    const uint16_t bulk_size = (speed == USB_SPEED_HIGH) ? USB_HS_BULK_PACKET : USB_FS_BULK_PACKET;
    patch_endpoint_packet_sizes(bulk_size);
    g_midiEndpoints[0].maxPacketSize = bulk_size;
    g_midiEndpoints[1].maxPacketSize = bulk_size;
    g_cdcDataEndpoints[0].maxPacketSize = bulk_size;
    g_cdcDataEndpoints[1].maxPacketSize = bulk_size;
}

const uint8_t *usb_descriptors_configuration(uint32_t *length)
{
    *length = sizeof(s_configuration_descriptor);
    return s_configuration_descriptor;
}

usb_status_t usb_descriptors_handle_event(uint32_t event, void *param)
{
    switch (event)
    {
        case kUSB_DeviceEventGetDeviceDescriptor:
        {
            usb_device_get_device_descriptor_struct_t *descriptor = param;
            descriptor->buffer = s_device_descriptor;
            descriptor->length = sizeof(s_device_descriptor);
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventGetDeviceQualifierDescriptor:
        {
            usb_device_get_device_qualifier_descriptor_struct_t *descriptor = param;
            descriptor->buffer = s_device_qualifier;
            descriptor->length = sizeof(s_device_qualifier);
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventGetConfigurationDescriptor:
        {
            usb_device_get_configuration_descriptor_struct_t *descriptor = param;
            if (descriptor->configuration != 0u)
            {
                return kStatus_USB_InvalidRequest;
            }
            descriptor->buffer = s_configuration_descriptor;
            descriptor->length = sizeof(s_configuration_descriptor);
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventGetStringDescriptor:
        {
            usb_device_get_string_descriptor_struct_t *descriptor = param;
            if ((descriptor->stringIndex >= (sizeof(s_strings) / sizeof(s_strings[0]))) ||
                ((descriptor->stringIndex != 0u) && (descriptor->languageId != 0x0409u)))
            {
                return kStatus_USB_InvalidRequest;
            }
            descriptor->buffer = s_strings[descriptor->stringIndex];
            descriptor->length = descriptor->buffer[0];
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventGetHidDescriptor:
        {
            usb_device_get_hid_descriptor_struct_t *descriptor = param;
            if (descriptor->interfaceNumber == USB_IFACE_KEYBOARD)
            {
                descriptor->buffer = &s_configuration_descriptor[CFG_KEYBOARD_HID_OFFSET];
            }
            else if (descriptor->interfaceNumber == USB_IFACE_UPDATER)
            {
                descriptor->buffer = &s_configuration_descriptor[CFG_UPDATER_HID_OFFSET];
            }
            else
            {
                return kStatus_USB_InvalidRequest;
            }
            descriptor->length = 9u;
            return kStatus_USB_Success;
        }
        case kUSB_DeviceEventGetHidReportDescriptor:
        {
            usb_device_get_hid_report_descriptor_struct_t *descriptor = param;
            if (descriptor->interfaceNumber == USB_IFACE_KEYBOARD)
            {
                descriptor->buffer = (uint8_t *)s_keyboard_report_descriptor;
                descriptor->length = sizeof(s_keyboard_report_descriptor);
            }
            else if (descriptor->interfaceNumber == USB_IFACE_UPDATER)
            {
                descriptor->buffer = (uint8_t *)s_updater_report_descriptor;
                descriptor->length = sizeof(s_updater_report_descriptor);
            }
            else
            {
                return kStatus_USB_InvalidRequest;
            }
            return kStatus_USB_Success;
        }
        default:
            return kStatus_USB_InvalidRequest;
    }
}
