#ifndef OPTICAL_KEY_H
#define OPTICAL_KEY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t pressed, armed, trough, peak, cooldown;
} optical_key_state_t;

typedef struct {
    uint8_t press, release, rapid, continuous, release_delta, press_delta;
    uint8_t wait_down, wait_up;
} optical_key_config_t;

/* Production 0x2000e354 with output endpoints 0/255. */
uint8_t optical_key_level(uint16_t lower, uint16_t upper, uint16_t sample);
/* Production 0x2001a918 normal/rapid hysteresis, without lighting side effects.
 * Returns +1 press, -1 release, 0 unchanged. Cooldowns are scan-pass counts. */
int optical_key_update(optical_key_state_t *state, const optical_key_config_t *config, uint8_t level);
/* Production external endpoint branch 0x20015dec, default control 0/10. */
bool optical_key_calibrate(uint16_t raw_lower, uint16_t raw_upper, uint16_t settled,
                           bool use_settled, uint16_t *lower, uint16_t *upper);

#endif
