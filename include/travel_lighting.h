#ifndef TRAVEL_LIGHTING_H
#define TRAVEL_LIGHTING_H
#include <stdbool.h>
#include <stdint.h>

#define LIGHTING_FRAME_SIZE 204u
typedef struct { uint8_t controller, red, green, blue; } lighting_channels_t;
typedef struct { uint8_t address, reg, size, fill, delay_ms; } lighting_op_t;
extern const lighting_channels_t g_lighting_channels[3][65];
extern const lighting_op_t g_lighting_primary[], g_lighting_secondary[], g_lighting_maintenance[];
extern const unsigned g_lighting_primary_count, g_lighting_secondary_count, g_lighting_maintenance_count;

/* White PWM, linear in the production endpoint-normalized optical range.
 * Does not use binary key state, actuation thresholds, gamma or a deadband. */
uint8_t lighting_travel_pwm(uint16_t raw, uint16_t lower, uint16_t upper);
void lighting_travel_frame(uint8_t profile, const uint16_t *raw, const uint16_t *lower,
                          const uint16_t *upper, bool valid, uint8_t *frame);

enum lighting_phase { LIGHT_OFF, LIGHT_LOW, LIGHT_HIGH, LIGHT_PRIMARY,
    LIGHT_SECONDARY, LIGHT_RUN, LIGHT_MAINTENANCE, LIGHT_FAULT };
typedef struct {
    uint8_t phase, profile, operation, stage, cycles, delay_ms;
    bool pending, requested, frame_valid;
    uint32_t since, last_frame, last_cycle, transfers, frames, errors;
    const char *fault;
    uint8_t desired[LIGHTING_FRAME_SIZE], snapshot[LIGHTING_FRAME_SIZE], tx[192];
} travel_lighting_t;
void travel_lighting_init(travel_lighting_t *s);
bool travel_lighting_start(travel_lighting_t *s, uint8_t profile, uint32_t now);
void travel_lighting_service(travel_lighting_t *s, uint32_t now);
void travel_lighting_frame(travel_lighting_t *s, const uint16_t *raw, const uint16_t *lower,
                           const uint16_t *upper, bool valid, uint32_t now);
#endif
