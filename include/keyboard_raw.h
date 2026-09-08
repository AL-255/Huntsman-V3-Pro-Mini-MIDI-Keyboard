#ifndef KEYBOARD_RAW_H
#define KEYBOARD_RAW_H
#include "keyboard_engine.h"
#define RAW_KEY_COUNT 65u
#define RAW_DEFAULT_PRESS 3500u
#define RAW_DEFAULT_RELEASE 3600u
typedef struct {
    float value;              /* clamp(raw counts/s / 4500000, 0, 1) */
    uint32_t captures;         /* completed fits, wrapping uint32 */
    uint16_t window[5];        /* rolling samples, never shared between keys */
    uint8_t write, pending;    /* next slot; trigger ages 0..4 as bits */
    bool ready, valid;         /* release-observed arming; result available */
} keyboard_velocity_t;
typedef struct {
    keyboard_engine_t engine;
    uint16_t raw[RAW_KEY_COUNT], press[RAW_KEY_COUNT], release[RAW_KEY_COUNT];
    bool down[RAW_KEY_COUNT];
    uint8_t count, profile;
    bool enabled, armed, valid, midi_mode;
    bool menu_managed;
    uint32_t revision;
    keyboard_velocity_t velocity[RAW_KEY_COUNT];
} keyboard_raw_t;
void keyboard_raw_init(keyboard_raw_t *s);
void keyboard_raw_invalidate(keyboard_raw_t *s);
void keyboard_raw_enable(keyboard_raw_t *s, bool enabled);
bool keyboard_raw_set(keyboard_raw_t *s, unsigned index, unsigned press, unsigned release);
bool keyboard_raw_set_all(keyboard_raw_t *s, unsigned press, unsigned release);
void keyboard_raw_frame(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                        uint8_t profile, bool valid);
#endif
