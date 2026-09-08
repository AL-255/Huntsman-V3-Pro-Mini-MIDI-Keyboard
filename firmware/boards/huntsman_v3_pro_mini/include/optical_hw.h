#ifndef HUNTSMAN_OPTICAL_HW_H
#define HUNTSMAN_OPTICAL_HW_H

#include <stdbool.h>
#include <stdint.h>

void optical_hw_init(void);
bool optical_hw_ready(void);
bool optical_hw_set_mode(uint8_t mode);
bool optical_hw_read(uint16_t *samples, uint8_t sensor_count);

#endif
