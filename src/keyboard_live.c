#include "keyboard_live.h"
#include "board.h"
#include "debug.h"
#include "keyboard_scan.h"
#include "keyboard_layout.h"
#include "optical_bus.h"
#include "optical_transport.h"
#include "usb_composite.h"
#include <string.h>

static optical_transport_t s_transport;
static keyboard_scan_t s_scan;
static keyboard_report_t s_sent;
static bool s_host_keys, s_trace, s_sent_valid;
static volatile bool s_usb_reset;
static uint32_t s_last_frame, s_last_report;

static void value(const char *label, uint32_t n)
{
    char text[12], *end = text + sizeof(text) - 1u;
    *end = '\0';
    do { *--end = (char)('0' + n % 10u); n /= 10u; } while (n);
    debug_write(label); debug_write(end);
}

static void config_status(void)
{
    const keyboard_config_t *s = &s_scan.engine.config;
    value("KEYS host=", s_host_keys); value(" fn=", s->fn); value(" mode=", s->mode);
    value(" act=", s->actuation); value(" rapid=", s->rapid);
    value(" enabled=", s->rapid_enabled); value(" saved=", s->saved_actuation);
    value(",", s->saved_rapid); value(" revision=", s->revision);
    debug_write(" RAM-only\r\n");
}

static void scan_status(void)
{
    value("SCAN phase=", s_transport.phase); value(" profile=", s_transport.profile);
    value(" count=", s_transport.count); value(" transfers=", s_transport.transfers);
    value(" frames=", s_transport.frames); value(" markers=", s_transport.markers);
    value(" errors=", s_transport.errors); value(" settled=", s_scan.ready);
    value(" valid=", s_scan.valid); value(" calibrated=", s_scan.calibrated);
    if (s_transport.fault) { debug_write(" fault="); debug_write(s_transport.fault); }
    debug_write("\r\n");
}

static void release_host(void)
{
    s_host_keys = false;
    s_sent_valid = false; /* retry neutral on busy USB, not only once */
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
    release_host();
}

void keyboard_live_usb_reset(void) { s_usb_reset = true; }

void keyboard_live_service(void)
{
    const uint32_t now = board_millis();
    if (s_usb_reset) { s_usb_reset = false; release_host(); }
    const uint8_t phase = s_transport.phase;
    const keyboard_config_t previous = s_scan.engine.config;
    if (optical_transport_service(&s_transport, now, optical_bus_ticks()))
    {
        s_last_frame = now;
        if (!s_scan.count) keyboard_scan_init(&s_scan, s_transport.profile);
        keyboard_scan_frame(&s_scan, s_transport.samples, s_transport.tables[4], s_transport.tables[6], event);
    }
    if (phase != s_transport.phase &&
        (s_transport.phase == OPT_FAULT || s_transport.phase == OPT_SCAN_READ)) scan_status();
    if (memcmp(&previous, &s_scan.engine.config, sizeof(previous))) config_status();
    if (s_host_keys && (!s_scan.valid || (uint32_t)(now - s_last_frame) >= 100u ||
                       s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready()))
    {
        release_host();
        debug_write("KEYS disarmed: stale/invalid scan or CDC disconnected\r\n");
    }
    keyboard_report_t report = {0};
    if (s_host_keys) report = s_scan.engine.report;
    if (!s_sent_valid || memcmp(&s_sent, &report, sizeof(report)) ||
        (uint32_t)(now - s_last_report) >= 1000u)
    {
        if (usb_keyboard_send(&report))
        {
            s_sent = report; s_sent_valid = true; s_last_report = now;
        }
    }
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool keyboard_live_command(const char *line)
{
    if (!strcmp(line, "help"))
    {
        debug_write("scan start | scan stop | scan status | scan sample XX (raw index hex)\r\n"
                    "keys on | keys off | keys status | trace on | trace off\r\n"
                    "Scan starts OFF; one attempt per boot. keys on requires neutral valid samples.\r\n");
        return false; /* also print isolated TEST help */
    }
    if (!strcmp(line, "status") || !strcmp(line, "scan status")) scan_status();
    else if (!strcmp(line, "scan start"))
    {
        debug_write(optical_transport_start(&s_transport, board_millis()) ?
                    "SCAN starting; host keys remain off\r\n" : "ERR scan already attempted; no automatic retry\r\n");
    }
    else if (!strcmp(line, "scan stop"))
    {
        release_host(); optical_transport_stop(&s_transport); scan_status();
    }
    else if (!strcmp(line, "keys status")) config_status();
    else if (!strcmp(line, "keys off")) { release_host(); config_status(); }
    else if (!strcmp(line, "keys on"))
    {
        if (s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready() ||
            (uint32_t)(board_millis() - s_last_frame) >= 100u || !keyboard_scan_neutral(&s_scan))
            debug_write("ERR keys not armed: scan must be fresh, settled, valid, and all keys released\r\n");
        else { s_host_keys = true; config_status(); }
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
