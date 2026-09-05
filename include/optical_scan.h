#ifndef HUNTSMAN_OPTICAL_SCAN_H
#define HUNTSMAN_OPTICAL_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_config.h"

#define OPTICAL_SETTLING_FRAMES 128u
#define OPTICAL_PRESS_THRESHOLD_Q16   0x5000u
#define OPTICAL_RELEASE_THRESHOLD_Q16 0x3800u

typedef struct
{
    uint16_t baseline[OPTICAL_MAX_SENSOR_COUNT];
    uint8_t pressed[OPTICAL_MAX_SENSOR_COUNT];
    uint16_t settling_frames;
    uint8_t sensor_count;
} optical_scan_state_t;

typedef struct
{
    uint64_t changed_mask_low;
    uint8_t changed_mask_high;
    uint8_t valid_samples;
} optical_scan_result_t;

void optical_scan_init(optical_scan_state_t *state, uint8_t sensor_count);
bool optical_scan_parse_response(const uint8_t *wire, size_t wire_length,
                                 uint16_t *samples, uint8_t sensor_count);
optical_scan_result_t optical_scan_process(optical_scan_state_t *state,
                                           const uint16_t *samples);
uint16_t optical_scan_travel_q16(uint16_t baseline, uint16_t sample);

#endif
