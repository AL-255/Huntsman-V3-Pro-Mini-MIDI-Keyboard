#ifndef HUNTSMAN_USB_DESCRIPTORS_H
#define HUNTSMAN_USB_DESCRIPTORS_H

#include <stdint.h>

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

enum
{
    USB_IFACE_KEYBOARD = 0,
    USB_IFACE_MIDI_CONTROL = 1,
    USB_IFACE_MIDI_STREAM = 2,
    USB_IFACE_UPDATER = 3,
    USB_IFACE_CDC_CONTROL = 4,
    USB_IFACE_CDC_DATA = 5,
    USB_IFACE_COUNT = 6,
};

#define USB_KEYBOARD_ENDPOINT 1u
#define USB_MIDI_ENDPOINT     2u
#define USB_CDC_NOTIFICATION_ENDPOINT 3u
#define USB_CDC_DATA_ENDPOINT 4u

#define USB_ENDPOINT_IN  0x80u
#define USB_ENDPOINT_OUT 0x00u

#define USB_FS_BULK_PACKET 64u
#define USB_HS_BULK_PACKET 512u
#define USB_KEYBOARD_PACKET 16u
#define USB_CDC_NOTIFICATION_PACKET 8u

extern usb_device_class_struct_t g_keyboardClass;
extern usb_device_class_struct_t g_updaterClass;
extern usb_device_class_struct_t g_cdcClass;
extern usb_device_endpoint_struct_t g_keyboardEndpoints[];
extern usb_device_endpoint_struct_t g_midiEndpoints[];
extern usb_device_endpoint_struct_t g_cdcControlEndpoints[];
extern usb_device_endpoint_struct_t g_cdcDataEndpoints[];

usb_status_t usb_descriptors_handle_event(uint32_t event, void *param);
void usb_descriptors_set_speed(uint8_t speed);
const uint8_t *usb_descriptors_configuration(uint32_t *length);

#endif
