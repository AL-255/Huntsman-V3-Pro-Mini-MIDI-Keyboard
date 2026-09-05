#ifndef OPTICAL_BUS_H
#define OPTICAL_BUS_H
#include <stdbool.h>
#include <stdint.h>
/* Hardware boundary. All functions return without waiting for an ASIC reply. */
void optical_bus_begin(void);
void optical_bus_route(void);
bool optical_bus_enable(void);
bool optical_bus_ready(void);
bool optical_bus_submit(uint8_t *tx, uint8_t *rx, uint16_t length);
int optical_bus_result(void); /* 0 pending, 1 completed, -1 error */
void optical_bus_quarantine(void); /* no blocking SDK abort; no buffer reuse */
uint32_t optical_bus_ticks(void); /* nominal 125 us CTIMER2 ticks */
#endif
