#include "debug.h"

#include <string.h>

#include "usb_composite.h"
#include "fsl_common.h"
#ifdef HUNTSMAN_KEYBOARD_DIAGNOSTICS
#include "scan_stream.h"
#endif

#define DEBUG_RING_SIZE 1024u
#define DEBUG_USB_CHUNK   128u

static uint8_t s_ring[DEBUG_RING_SIZE];
static uint16_t s_head;
static uint16_t s_tail;
static volatile bool s_sending;

void debug_init(void)
{
    s_head = s_tail = 0u;
    s_sending = false;
}

void debug_write_bytes(const uint8_t *data, size_t length)
{
#ifdef HUNTSMAN_KEYBOARD_DIAGNOSTICS
    if (scan_stream_enabled()) return;
#endif
    for (size_t i = 0; i < length; ++i)
    {
        const uint16_t next = (uint16_t)((s_head + 1u) % DEBUG_RING_SIZE);
        if (next == s_tail)
        {
            break;
        }
        s_ring[s_head] = data[i];
        s_head = next;
    }
}

void debug_write(const char *message)
{
    debug_write_bytes((const uint8_t *)message, strlen(message));
}

void debug_write_hex16(const char *label, uint16_t value)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t line[32];
    size_t length = 0u;
    while ((*label != '\0') && (length < 24u))
    {
        line[length++] = (uint8_t)*label++;
    }
    line[length++] = '0';
    line[length++] = 'x';
    line[length++] = (uint8_t)hex[(value >> 12u) & 0x0fu];
    line[length++] = (uint8_t)hex[(value >> 8u) & 0x0fu];
    line[length++] = (uint8_t)hex[(value >> 4u) & 0x0fu];
    line[length++] = (uint8_t)hex[value & 0x0fu];
    line[length++] = '\r';
    line[length++] = '\n';
    debug_write_bytes(line, length);
}

void debug_usb_configured(void)
{
    s_sending = false;
}

void debug_service(void)
{
#ifdef HUNTSMAN_KEYBOARD_DIAGNOSTICS
    if (scan_stream_enabled()) s_tail = s_head;
    if (scan_stream_service()) return;
#endif
    static uint8_t packet[DEBUG_USB_CHUNK];
    const uint32_t irq = DisableGlobalIRQ();
    if (s_sending || !usb_cdc_ready() || (s_head == s_tail))
    {
        EnableGlobalIRQ(irq);
        return;
    }
    uint32_t length = 0u;
    while ((s_tail != s_head) && (length < sizeof(packet)))
    {
        packet[length++] = s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) % DEBUG_RING_SIZE);
    }
    s_sending = true;
    if (!usb_cdc_write(packet, length))
    {
        s_sending = false;
        /* Keep the ring consistent on a transient busy return. */
        s_tail = (uint16_t)((s_tail + DEBUG_RING_SIZE - length) % DEBUG_RING_SIZE);
    }
    EnableGlobalIRQ(irq);
}

/* Called by the USB CDC send-complete path. */
void debug_cdc_send_complete(void)
{
    s_sending = false;
#ifdef HUNTSMAN_KEYBOARD_DIAGNOSTICS
    scan_stream_complete();
#endif
}
