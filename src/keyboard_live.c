#include "keyboard_live.h"
#include "board.h"
#include "debug.h"
#include "keyboard_scan.h"
#include "keyboard_layout.h"
#include "optical_bus.h"
#include "optical_transport.h"
#include "usb_composite.h"
#include "scan_stream.h"
#include <string.h>
#ifdef HUNTSMAN_TRAVEL_LIGHTING
#include "travel_lighting.h"
static travel_lighting_t s_lighting;
#endif

static optical_transport_t s_transport;
static keyboard_scan_t s_scan;
static keyboard_report_t s_sent;
static bool s_host_keys, s_trace, s_sent_valid;
static volatile bool s_usb_reset;
static uint32_t s_last_frame, s_last_report;
static bool s_stream_requested;
#ifdef HUNTSMAN_KEYBOARD_MODE
#include "keyboard_raw.h"
#include "keyboard_midi.h"
static keyboard_raw_t s_raw;
static keyboard_midi_t s_midi;
static uint32_t s_gui_sequence, s_gui_ack, s_last_gui;
static uint8_t s_gui_result;

static void gui16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8u; }
static void gui32(uint8_t *p, uint32_t v) { gui16(p, v); gui16(p + 2, v >> 16u); }
static void gui_snapshot(uint32_t now)
{
    if (!scan_stream_gui_enabled() || (uint32_t)(now - s_last_gui) < 33u) return;
    s_last_gui = now;
    uint8_t out[SCAN_STREAM_GUI_SIZE] = {0};
    memcpy(out, "HKG4", 4u); gui16(out + 4, sizeof(out)); out[6] = 4u;
    out[7] = s_raw.profile; out[8] = s_raw.count;
    out[9] = s_raw.enabled | (s_raw.armed << 1u) | (s_raw.valid << 2u) |
             ((s_transport.phase == OPT_FAULT) << 3u) | ((s_lighting.phase == LIGHT_FAULT) << 4u) |
             ((s_raw.engine.config.fn != 0u) << 5u);
    out[10] = s_gui_result;
    out[11] = s_raw.engine.config.mode;
    gui32(out + 12, s_gui_sequence++); gui32(out + 16, s_raw.revision);
    gui32(out + 20, s_gui_ack); gui32(out + 24, s_transport.errors);
    gui32(out + 28, s_lighting.errors);
    for (unsigned i = 0; i < s_raw.count; ++i) {
        gui16(out + 32 + i*2, s_raw.raw[i]);
        gui16(out + 162 + i*2, s_raw.press[i]);
        gui16(out + 292 + i*2, s_raw.release[i]);
        if (s_raw.down[i]) out[422 + i/8] |= 1u << (i%8);
        const keyboard_velocity_t *v = &s_raw.velocity[i];
        _Static_assert(sizeof(float) == sizeof(uint32_t), "GUI float32 size");
        uint32_t bits;
        memcpy(&bits, &v->value, sizeof(bits)); /* preserve IEEE-754 bits */
        gui32(out + 447 + i*4, bits);
        gui32(out + 707 + i*4, v->captures);
        out[967 + i] = v->ready | (v->valid << 1u) | ((v->pending != 0u) << 2u);
    }
    memcpy(out + 431, &s_sent, sizeof(s_sent)); /* last accepted USB submission */
    out[9] &= ~32u;
    for (unsigned i = 0; i < s_raw.count; ++i) {
        out[1036 + i] = s_midi.mapping[i];
        if (s_raw.down[i] && keyboard_key_for_sensor(s_raw.profile, i) == KEY_ID_FN) out[9] |= 32u;
    }
    out[1032] = s_midi.mode; out[1033] = (uint8_t)s_midi.octave;
    out[1034] = 1; out[1035] = s_midi.panic != 0;
    gui32(out + 1104, s_midi.errors); gui32(out + 1108, s_midi.changes);
    uint32_t checksum = 0;
    for (unsigned i = 0; i < sizeof(out)-4u; i += 2u) checksum += out[i] | (uint16_t)out[i+1] << 8u;
    gui32(out + sizeof(out)-4u, checksum);
    scan_stream_gui_push(out);
}
#endif

static void value(const char *label, uint32_t n)
{
    char text[12], *end = text + sizeof(text) - 1u;
    *end = '\0';
    do { *--end = (char)('0' + n % 10u); n /= 10u; } while (n);
    debug_write(label); debug_write(end);
}

static void config_status(void)
{
#ifdef HUNTSMAN_KEYBOARD_MODE
    const keyboard_config_t *s = &s_raw.engine.config;
#else
    const keyboard_config_t *s = &s_scan.engine.config;
#endif
    value("KEYS host=", s_host_keys); value(" fn=", s->fn); value(" mode=", s->mode);
    value(" act=", s->actuation); value(" rapid=", s->rapid);
    value(" enabled=", s->rapid_enabled); value(" saved=", s->saved_actuation);
    value(",", s->saved_rapid); value(" revision=", s->revision);
    debug_write(" RAM-only\r\n");
#ifdef HUNTSMAN_KEYBOARD_MODE
    value("RAW enabled=", s_raw.enabled); value(" armed=", s_raw.armed);
    value(" valid=", s_raw.valid); value(" revision=", s_raw.revision);
    debug_write(" per-key Schmitt; press<release; RAM-only\r\n");
#endif
}

static void scan_status(void)
{
    value("SCAN phase=", s_transport.phase); value(" profile=", s_transport.profile);
    value(" count=", s_transport.count); value(" transfers=", s_transport.transfers);
    value(" frames=", s_transport.frames); value(" markers=", s_transport.markers);
    value(" errors=", s_transport.errors); value(" settled=", s_scan.ready);
    value(" valid=", s_scan.valid); value(" calibrated=", s_scan.calibrated);
    value(" stream_dropped=", scan_stream_dropped());
    if (s_transport.fault) { debug_write(" fault="); debug_write(s_transport.fault); }
    debug_write("\r\n");
}

#ifdef HUNTSMAN_TRAVEL_LIGHTING
static void lighting_status(void)
{
    value("LIGHT phase=", s_lighting.phase); value(" on=", s_lighting.requested);
    value(" profile=", s_lighting.profile); value(" transfers=", s_lighting.transfers);
    value(" frames=", s_lighting.frames); value(" errors=", s_lighting.errors);
    value(" calibrated=", s_scan.calibrated); value(" count=", s_scan.count);
    if (s_lighting.fault) { debug_write(" fault="); debug_write(s_lighting.fault); }
    debug_write(" PWM linear in optical endpoints; not measured millimeters\r\n");
}
#endif

static void release_host(void)
{
    s_host_keys = false;
    s_sent_valid = false; /* retry neutral on busy USB, not only once */
#ifdef HUNTSMAN_KEYBOARD_MODE
    keyboard_raw_invalidate(&s_raw);
#endif
}

static void event(uint8_t key, bool down, uint8_t level)
{
    if (!s_trace) return;
    static const char digits[] = "0123456789abcdef";
    char id[3] = {digits[key >> 4u], digits[key & 15u], '\0'};
    debug_write("KEY "); debug_write(id); debug_write(down ? " down" : " up");
    value(" level=", level); debug_write("\r\n");
}

void keyboard_live_init(void)
{
    optical_transport_init(&s_transport);
    keyboard_scan_init(&s_scan, 0u);
    scan_stream_init();
    s_stream_requested = true;
#ifdef HUNTSMAN_KEYBOARD_MODE
    keyboard_raw_init(&s_raw);
    keyboard_midi_init(&s_midi);
    s_gui_sequence = s_gui_ack = s_last_gui = 0u;
    s_gui_result = 0u;
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    travel_lighting_init(&s_lighting);
#endif
    release_host();
}

void keyboard_live_usb_reset(void) { s_usb_reset = true; scan_stream_usb_reset(); }

void keyboard_live_service(void)
{
    const uint32_t now = board_millis();
    if (s_usb_reset) { s_usb_reset = false; release_host(); }
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    /* This preset is a working effect, not an OFF-by-default diagnostic.
     * USB first; one optical route attempt; never restart on a fault/reset. */
    if (s_transport.phase == OPT_OFF && usb_composite_ready())
        (void)optical_transport_start(&s_transport, now);
#endif
    const uint8_t phase = s_transport.phase;
    const keyboard_config_t previous = s_scan.engine.config;
    if (optical_transport_service(&s_transport, now, optical_bus_ticks()))
    {
        s_last_frame = now;
        if (s_stream_requested)
        {
            scan_stream_start();
            scan_stream_push(s_transport.samples, s_transport.count, s_transport.profile, optical_bus_ticks());
        }
        if (!s_scan.count) keyboard_scan_init(&s_scan, s_transport.profile);
        keyboard_scan_frame(&s_scan, s_transport.samples, s_transport.tables[4], s_transport.tables[6], event);
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_raw_frame(&s_raw, s_transport.samples, s_transport.count, s_transport.profile,
                           s_scan.ready && s_scan.valid && usb_composite_ready());
        keyboard_midi_frame(&s_midi, &s_raw, s_scan.lower, s_scan.upper, now);
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
        if (s_lighting.phase == LIGHT_OFF)
            (void)travel_lighting_start(&s_lighting, s_transport.profile, now);
        travel_lighting_frame(&s_lighting, s_transport.samples, s_scan.lower, s_scan.upper,
                              s_scan.ready && s_scan.valid, now);
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_midi_lights(&s_midi, s_lighting.desired, now);
#endif
#endif
    }
    if (phase != s_transport.phase &&
        (s_transport.phase == OPT_FAULT || s_transport.phase == OPT_SCAN_READ)) scan_status();
    if (memcmp(&previous, &s_scan.engine.config, sizeof(previous))) config_status();
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    const uint8_t light_phase = s_lighting.phase;
    if (s_transport.phase != OPT_SCAN_READ || !usb_composite_ready()) s_lighting.frame_valid = false;
    travel_lighting_service(&s_lighting, now);
    if (light_phase != s_lighting.phase &&
        (s_lighting.phase == LIGHT_RUN || s_lighting.phase == LIGHT_FAULT)) lighting_status();
#endif
#ifdef HUNTSMAN_KEYBOARD_MODE
    if ((uint32_t)(now - s_last_frame) >= 100u || s_transport.phase != OPT_SCAN_READ || !usb_composite_ready())
        keyboard_raw_invalidate(&s_raw);
    s_host_keys = s_raw.armed;
    keyboard_midi_guard(&s_midi, &s_raw);
    keyboard_midi_service(&s_midi, now, usb_midi_send);
#else
    if (s_host_keys && (!s_scan.valid || (uint32_t)(now - s_last_frame) >= 100u ||
                       s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready()))
    {
        release_host();
        debug_write("KEYS disarmed: stale/invalid scan or CDC disconnected\r\n");
    }
#endif
    keyboard_report_t report = {0};
#ifdef HUNTSMAN_KEYBOARD_MODE
    if (s_host_keys && !s_midi.mode) report = s_raw.engine.report;
#else
    if (s_host_keys) report = s_scan.engine.report;
#endif
    if (!s_sent_valid || memcmp(&s_sent, &report, sizeof(report)) ||
        (uint32_t)(now - s_last_report) >= 1000u)
    {
        if (usb_keyboard_send(&report))
        {
            s_sent = report; s_sent_valid = true; s_last_report = now;
        }
    }
#ifdef HUNTSMAN_KEYBOARD_MODE
    gui_snapshot(now);
#endif
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decimal(const char **text, uint32_t *value)
{
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    *value = 0u;
    while (*p >= '0' && *p <= '9')
    {
        const unsigned digit = (unsigned)(*p++ - '0');
        if (*value > (UINT32_MAX - digit) / 10u) return false;
        *value = *value * 10u + digit;
    }
    *text = p;
    return true;
}

bool keyboard_live_command(const char *line)
{
#ifdef HUNTSMAN_KEYBOARD_MODE
    if (!strcmp(line, "stream gui")) {
        scan_stream_gui(); s_stream_requested = true; return true;
    }
    if (!strncmp(line, "cfg ", 4u)) {
        const char *p = strchr(line + 4, ' ');
        uint32_t id = 0, a = 0, b = 0, c = 0;
        if (!p) return true;
        ++p;
        if (!decimal(&p, &id) || !id) return true;
        s_gui_ack = id; s_gui_result = 2u;
        if (!strncmp(line, "cfg get ", 8u) && !*p) s_gui_result = 1u;
        else if (!strncmp(line, "cfg enable ", 11u) && *p++ == ' ' && decimal(&p, &a) && !*p && a <= 1u) {
            keyboard_raw_enable(&s_raw, a != 0u); s_gui_result = 1u;
        }
        else if (!strncmp(line, "cfg set ", 8u) && *p++ == ' ' && decimal(&p, &a) &&
                 *p++ == ' ' && decimal(&p, &b) && *p++ == ' ' && decimal(&p, &c) && !*p &&
                 keyboard_raw_set(&s_raw, a, b, c)) s_gui_result = 1u;
        else if (!strncmp(line, "cfg all ", 8u) && *p++ == ' ' && decimal(&p, &a) &&
                 *p++ == ' ' && decimal(&p, &b) && !*p &&
                 keyboard_raw_set_all(&s_raw, a, b)) s_gui_result = 1u;
        else if (!strncmp(line, "cfg midi ", 9u) && *p++ == ' ' && decimal(&p, &a) &&
                 *p++ == ' ' && decimal(&p, &b) && !*p &&
                 keyboard_midi_map(&s_midi, &s_raw, a, b)) s_gui_result = 1u;
        return true;
    }
#endif
    if (!strcmp(line, "help"))
    {
        debug_write("scan start | scan stop | scan status | scan sample XX (raw index hex)\r\n"
                    "stream on | stream off (HKS1 binary uint16 scan frames; default on when scanning)\r\n"
                    "stream key N [session] (HKL1; decimal threshold 1..4096, optional uint32 session)\r\n"
                    "keys on | keys off | keys status | trace on | trace off\r\n"
                    "keys on requires neutral valid samples; one scan attempt per boot.\r\n");
#ifdef HUNTSMAN_KEYBOARD_MODE
        debug_write("stream gui | cfg get ID | cfg set ID SENSOR PRESS RELEASE | cfg all ID PRESS RELEASE | cfg enable ID 0/1\r\n"
                    "cfg midi ID SENSOR NOTE (0..127, 255=unmapped); Fn+Enter toggles MIDI; LCtrl/LAlt octave-/+\r\n"
                    "Standalone raw keyboard auto-arms after neutral scan; settings RAM-only.\r\n");
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
        debug_write("light on | light off | light status\r\n"
                    "Scanning and travel lighting start automatically after USB configuration.\r\n");
#else
        debug_write("Scan starts OFF.\r\n");
#endif
        return false; /* also print isolated TEST help */
    }
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    if (!strcmp(line, "light on")) { s_lighting.requested = true; lighting_status(); return true; }
    if (!strcmp(line, "light off")) { s_lighting.requested = false; lighting_status(); return true; }
    if (!strcmp(line, "light status")) { lighting_status(); return true; }
#endif
    if (!strncmp(line, "stream key ", 11u))
    {
        uint32_t threshold = 0u, session = 0u;
        const char *p = line + 11u;
        bool valid = decimal(&p, &threshold) && threshold && threshold <= 4096u;
        if (valid && *p == ' ') { ++p; valid = decimal(&p, &session); }
        if (!valid || *p)
        { debug_write("ERR stream key threshold[1..4096] [uint32 session]\r\n"); return true; }
        scan_stream_last_key((uint16_t)threshold, session);
        s_stream_requested = true;
    }
    else if (!strcmp(line, "stream on")) { scan_stream_whole(); s_stream_requested = true; }
    else if (!strcmp(line, "stream off")) { s_stream_requested = false; scan_stream_stop(); }
    else if (!strcmp(line, "status") || !strcmp(line, "scan status")) scan_status();
    else if (!strcmp(line, "scan start"))
    {
        debug_write(optical_transport_start(&s_transport, board_millis()) ?
                    "SCAN starting; host keys remain off\r\n" : "ERR scan already attempted; no automatic retry\r\n");
    }
    else if (!strcmp(line, "scan stop"))
    {
        s_stream_requested = false; scan_stream_stop();
        release_host(); optical_transport_stop(&s_transport); scan_status();
    }
    else if (!strcmp(line, "keys status")) config_status();
    else if (!strcmp(line, "keys off")) {
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_raw_enable(&s_raw, false);
#endif
        release_host(); config_status();
    }
    else if (!strcmp(line, "keys on"))
    {
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_raw_enable(&s_raw, true);
        config_status();
#else
        if (s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready() ||
            (uint32_t)(board_millis() - s_last_frame) >= 100u || !keyboard_scan_neutral(&s_scan))
            debug_write("ERR keys not armed: scan must be fresh, settled, valid, and all keys released\r\n");
        else { s_host_keys = true; config_status(); }
#endif
    }
    else if (!strcmp(line, "trace on")) { s_trace = true; debug_write("TRACE on\r\n"); }
    else if (!strcmp(line, "trace off")) { s_trace = false; debug_write("TRACE off\r\n"); }
    else if (strlen(line) == 14u && !memcmp(line, "scan sample ", 12u) &&
             hex(line[12]) >= 0 && hex(line[13]) >= 0)
    {
        const unsigned i = (unsigned)(hex(line[12]) * 16 + hex(line[13]));
        if (i >= s_scan.count) debug_write("ERR sensor index/layout unavailable\r\n");
        else
        {
            value("SAMPLE index=", i); value(" key=", keyboard_key_for_sensor(s_transport.profile, i));
            value(" raw=", s_scan.raw[i]); value(" lower=", s_scan.lower[i]);
            value(" upper=", s_scan.upper[i]); value(" level=", s_scan.levels[i]);
            value(" pressed=", s_scan.keys[i].pressed); debug_write("\r\n");
        }
    }
    else return false;
    return true;
}
