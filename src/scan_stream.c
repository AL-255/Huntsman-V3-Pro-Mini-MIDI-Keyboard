#include "scan_stream.h"
#include "usb_composite.h"
#include "fsl_common.h"
#include <string.h>

/* 4 ms backlog at 8k records/s, plus a stable four-record USB transfer.
 * Never use the lossy text ring for binary records. Producer/service: main;
 * completion/reset: USB ISR. Only complete records are queued or dropped. */
#define RECORDS 32u
#define BATCH 4u
static uint8_t s_records[RECORDS][SCAN_STREAM_RECORD_SIZE];
static uint8_t s_packet[BATCH * SCAN_STREAM_RECORD_SIZE];
static unsigned s_head, s_tail, s_count;
static uint32_t s_sequence, s_dropped;
static bool s_enabled;
static volatile bool s_busy, s_reset;

static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8u; }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16u); }

void scan_stream_init(void)
{
    s_head = s_tail = s_count = 0u;
    s_sequence = s_dropped = 0u;
    s_enabled = s_busy = s_reset = false;
}
void scan_stream_start(void) { s_enabled = true; }
void scan_stream_stop(void)
{
    s_enabled = false;
    s_dropped += s_count;
    s_head = s_tail = s_count = 0u;
    /* The in-flight packet remains immutable until completion or reset. */
}
bool scan_stream_active(void) { return s_enabled || s_busy; }
bool scan_stream_enabled(void) { return s_enabled; }
uint32_t scan_stream_dropped(void) { return s_dropped; }
void scan_stream_complete(void) { s_busy = false; }
void scan_stream_usb_reset(void) { s_reset = true; s_busy = false; }

void scan_stream_push(const uint16_t *samples, uint8_t count, uint8_t profile, uint32_t tick)
{
    if (!s_enabled || !count || count > 65u) return;
    const uint32_t sequence = s_sequence++;
    if (s_reset || !usb_cdc_ready() || s_count == RECORDS) { ++s_dropped; return; }
    uint8_t *out = s_records[s_head];
    memset(out, 0, SCAN_STREAM_RECORD_SIZE);
    memcpy(out, "HKS1", 4u);
    le16(out + 4u, SCAN_STREAM_RECORD_SIZE);
    out[6] = count; out[7] = profile;
    le32(out + 8u, sequence);
    le32(out + 12u, tick);
    le32(out + 16u, s_dropped);
    for (unsigned i = 0; i < count; ++i)
    {
        le16(out + 24u + i * 2u, samples[i]);
        if (!samples[i] || samples[i] > 4096u) out[20] = 1u;
    }
    uint32_t checksum = 0u;
    for (unsigned i = 0; i < 156u; i += 2u) checksum += out[i] | (uint16_t)out[i + 1u] << 8u;
    le32(out + 156u, checksum);
    s_head = (s_head + 1u) % RECORDS;
    ++s_count;
}

bool scan_stream_service(void)
{
    const uint32_t irq = DisableGlobalIRQ();
    if (s_reset || !usb_cdc_ready())
    {
        s_reset = false;
        s_dropped += s_count;
        s_head = s_tail = s_count = 0u;
    }
    if (!s_enabled || s_busy || !s_count)
    {
        const bool owns = s_enabled || s_busy;
        EnableGlobalIRQ(irq);
        return owns;
    }
    const unsigned count = s_count < BATCH ? s_count : BATCH;
    for (unsigned i = 0; i < count; ++i)
        memcpy(s_packet + i * SCAN_STREAM_RECORD_SIZE,
               s_records[(s_tail + i) % RECORDS], SCAN_STREAM_RECORD_SIZE);
    s_busy = true;
    if (usb_cdc_write(s_packet, count * SCAN_STREAM_RECORD_SIZE))
    {
        s_tail = (s_tail + count) % RECORDS;
        s_count -= count;
    }
    else s_busy = false;
    EnableGlobalIRQ(irq);
    return true;
}
