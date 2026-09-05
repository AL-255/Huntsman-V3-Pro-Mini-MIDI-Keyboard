#include "scan_stream.h"
#include "usb_composite.h"
#include "fsl_common.h"
#include <string.h>

/* Whole mode: 4 ms backlog at 8k records/s; compact: 32 ms at that rate.
 * Shared 5120-byte ring; stable 1152-byte storage fits one GUI snapshot.
 * Whole/compact transfers retain their original 640-byte batching limits.
 * Never use the lossy text ring for binary records. Producer/service: main;
 * completion/reset: USB ISR. Only complete records are queued or dropped. */
#define RECORDS 32u
#define BATCH 4u
static uint8_t s_records[RECORDS * SCAN_STREAM_RECORD_SIZE];
static uint8_t s_packet[SCAN_STREAM_GUI_SIZE];
static unsigned s_head, s_tail, s_count;
static uint32_t s_sequence, s_dropped;
static bool s_enabled;
static volatile bool s_busy, s_reset;
static bool s_key_mode, s_key_fault, s_fault_sent, s_first;
static bool s_gui_mode;
static bool s_pressed[65];
static uint8_t s_selected, s_profile;
static uint16_t s_threshold;
static uint32_t s_session;

static unsigned record_size(void) { return s_gui_mode ? SCAN_STREAM_GUI_SIZE : s_key_mode ? SCAN_STREAM_KEY_SIZE : SCAN_STREAM_RECORD_SIZE; }
static unsigned capacity(void) { return sizeof(s_records) / record_size(); }

static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8u; }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16u); }

void scan_stream_init(void)
{
    s_head = s_tail = s_count = 0u;
    s_sequence = s_dropped = 0u;
    s_enabled = s_busy = s_reset = false;
    s_key_mode = s_key_fault = s_fault_sent = false;
    s_gui_mode = false;
}
void scan_stream_last_key(uint16_t threshold, uint32_t session)
{
    scan_stream_stop(); /* explicit session boundary; pending IN stays immutable */
    s_gui_mode = false;
    s_key_mode = s_first = true;
    s_key_fault = s_fault_sent = false;
    s_sequence = 0u;
    s_threshold = threshold;
    s_session = session;
    s_selected = 255u;
    s_profile = 0u;
    memset(s_pressed, 0, sizeof(s_pressed));
    scan_stream_start();
}
void scan_stream_whole(void)
{
    if (!s_key_mode && !s_gui_mode) return;
    scan_stream_stop();
    s_key_mode = s_key_fault = s_fault_sent = false;
    s_gui_mode = false;
}
void scan_stream_gui(void)
{
    scan_stream_stop();
    s_key_mode = s_key_fault = s_fault_sent = false;
    s_gui_mode = true;
    scan_stream_start();
}
bool scan_stream_gui_enabled(void) { return s_gui_mode && s_enabled; }
void scan_stream_gui_push(const uint8_t report[SCAN_STREAM_GUI_SIZE])
{
    if (!scan_stream_gui_enabled() || !usb_cdc_ready()) return;
    /* GUI telemetry is explicitly latest-only. The USB-owned packet is a
     * separate copy; overwrite only the unsent snapshot, never pending IN. */
    memcpy(s_records, report, SCAN_STREAM_GUI_SIZE);
    s_head = 1u; s_tail = 0u; s_count = 1u;
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

static void key_record(uint8_t *out, uint16_t raw, uint8_t flags)
{
    memcpy(out, "HKL1", 4u);
    le32(out + 4u, s_session);
    le32(out + 8u, s_sequence++);
    le16(out + 12u, raw);
    out[14] = s_selected;
    out[15] = flags | (s_first ? 1u : 0u);
    s_first = false;
    le16(out + 16u, s_threshold);
    uint16_t checksum = 0u;
    for (unsigned i = 0; i < 18u; i += 2u) checksum += out[i] | (uint16_t)out[i + 1u] << 8u;
    le16(out + 18u, checksum);
}
void scan_stream_complete(void) { s_busy = false; }
void scan_stream_usb_reset(void) { s_reset = true; s_busy = false; }

void scan_stream_push(const uint16_t *samples, uint8_t count, uint8_t profile, uint32_t tick)
{
    if (!s_enabled || !count || count > 65u) return;
    if (s_gui_mode) return;
    if (s_key_mode)
    {
        if (s_key_fault) return; /* fail-stop until an explicit new session */
        if (s_reset || !usb_cdc_ready() || s_count == capacity())
        {
            ++s_dropped;
            s_key_fault = true;
            return;
        }
        uint8_t flags = 0u, newest = 255u;
        if (s_profile && s_profile != profile) flags |= 4u;
        s_profile = profile;
        for (unsigned i = 0; i < count; ++i)
        {
            if (!samples[i] || samples[i] > 4096u) flags |= 4u;
            const bool pressed = samples[i] < s_threshold;
            if (pressed && !s_pressed[i] && newest == 255u) newest = i;
            s_pressed[i] = pressed;
        }
        if (newest != 255u) s_selected = newest;
        key_record(s_records + s_head * SCAN_STREAM_KEY_SIZE,
                   s_selected < count ? samples[s_selected] : 0u, flags);
        s_head = (s_head + 1u) % capacity();
        ++s_count;
        return;
    }
    const uint32_t sequence = s_sequence++;
    if (s_reset || !usb_cdc_ready() || s_count == RECORDS) { ++s_dropped; return; }
    uint8_t *out = s_records + s_head * SCAN_STREAM_RECORD_SIZE;
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
        if (s_key_mode && s_enabled) s_key_fault = true;
        s_reset = false;
        s_dropped += s_count;
        s_head = s_tail = s_count = 0u;
    }
    if (s_key_mode && s_enabled && s_key_fault && !s_fault_sent && !s_busy && !s_count && usb_cdc_ready())
    {
        key_record(s_records, 0u, 2u);
        s_head = 1u; s_tail = 0u; s_count = 1u;
        s_fault_sent = true;
    }
    if (!s_enabled || s_busy || !s_count || !usb_cdc_ready())
    {
        const bool owns = s_enabled || s_busy;
        EnableGlobalIRQ(irq);
        return owns;
    }
    const unsigned size = record_size();
    const unsigned batch = s_gui_mode ? 1u : BATCH * SCAN_STREAM_RECORD_SIZE / size;
    const unsigned count = s_count < batch ? s_count : batch;
    for (unsigned i = 0; i < count; ++i)
        memcpy(s_packet + i * size,
               s_records + ((s_tail + i) % capacity()) * size, size);
    s_busy = true;
    if (usb_cdc_write(s_packet, count * size))
    {
        s_tail = (s_tail + count) % capacity();
        s_count -= count;
    }
    else s_busy = false;
    EnableGlobalIRQ(irq);
    return true;
}
