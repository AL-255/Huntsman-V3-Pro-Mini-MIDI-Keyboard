#ifndef KEYBOARD_CONFIG_H
#define KEYBOARD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

enum { KEY_CONFIG_NORMAL = 0, KEY_CONFIG_ACTUATION = 1, KEY_CONFIG_RAPID = 2 };

typedef struct {
    uint8_t mode, fn, actuation, rapid, saved_actuation, saved_rapid;
    uint8_t dirty, rapid_enabled, profile, locked;
    uint32_t revision;
} keyboard_config_t;

void keyboard_config_init(keyboard_config_t *state, uint8_t profile);
/* fn_at_press is latched per physical key, matching production event routing. */
bool keyboard_config_event(keyboard_config_t *state, uint8_t key, bool down, bool fn_at_press);
uint16_t keyboard_config_actuation_q16(const keyboard_config_t *state);
uint16_t keyboard_config_release_q16(const keyboard_config_t *state);
uint16_t keyboard_config_rapid_q16(const keyboard_config_t *state);

#endif
