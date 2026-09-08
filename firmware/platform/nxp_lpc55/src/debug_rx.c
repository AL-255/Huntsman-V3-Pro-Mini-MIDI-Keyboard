#include "debug_rx.h"

#include "fsl_common.h"

#define RX_SIZE 1024u
static uint8_t s_rx[RX_SIZE];
static volatile uint16_t s_head, s_tail;
static volatile bool s_overflow;
static volatile bool s_reset;

void debug_rx_reset(void)
{
    s_head = s_tail = 0u;
    s_overflow = false;
    s_reset = true;
}

void debug_rx_receive(const uint8_t *data, size_t length)
{
    if (data == NULL) return;
    for (size_t i = 0; i < length; ++i)
    {
        const uint16_t next = (s_head + 1u) % RX_SIZE;
        if (next == s_tail)
        {
            s_overflow = true;
            return;
        }
        s_rx[s_head] = data[i];
        s_head = next;
    }
}

void debug_rx_service(keyboard_console_t *console)
{
    /* Parse in main, never inside a USB interrupt. Bounded work per pass. */
    for (unsigned i = 0; i < 32u; ++i)
    {
        const uint32_t irq = DisableGlobalIRQ();
        if (s_reset)
        {
            console->length = 0u;
            console->discard = false;
            s_reset = false;
        }
        if (s_overflow)
        {
            s_tail = s_head;
            s_overflow = false;
            EnableGlobalIRQ(irq);
            keyboard_console_overflow(console);
            return;
        }
        if (s_tail == s_head)
        {
            EnableGlobalIRQ(irq);
            return;
        }
        const uint8_t byte = s_rx[s_tail];
        s_tail = (s_tail + 1u) % RX_SIZE;
        EnableGlobalIRQ(irq);
        keyboard_console_feed(console, byte);
    }
}
