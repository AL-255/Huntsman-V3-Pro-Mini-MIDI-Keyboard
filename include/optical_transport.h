#ifndef OPTICAL_TRANSPORT_H
#define OPTICAL_TRANSPORT_H
#include <stdbool.h>
#include <stdint.h>
enum optical_phase { OPT_OFF, OPT_ROUTE, OPT_ENABLE, OPT_PROBE, OPT_PROBE_REPLY,
    OPT_TABLE_MODE, OPT_TABLE_READ, OPT_SCAN_MODE, OPT_SCAN_READ, OPT_STOPPED, OPT_FAULT };
typedef struct {
    uint8_t phase, selector, profile, count, skip;
    bool pending, enabled;
    uint32_t since, tick, frames, errors, markers, transfers;
    uint8_t tx[197], rx[197];
    uint8_t tables[10][195];
    uint16_t samples[65];
    const char *fault;
} optical_transport_t;
void optical_transport_init(optical_transport_t *s);
bool optical_transport_start(optical_transport_t *s, uint32_t now);
void optical_transport_stop(optical_transport_t *s);
/* Returns true for a newly accepted A0 frame only. Never waits/spins. */
bool optical_transport_service(optical_transport_t *s, uint32_t now, uint32_t tick);
#endif
