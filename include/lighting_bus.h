#ifndef LIGHTING_BUS_H
#define LIGHTING_BUS_H
#include <stdbool.h>
#include <stdint.h>
void lighting_bus_init(void);
void lighting_bus_enable_pins(bool high);
bool lighting_bus_submit(uint8_t address, uint8_t reg, uint8_t *data, uint8_t size);
int lighting_bus_result(void);
void lighting_bus_quarantine(void);
#endif
