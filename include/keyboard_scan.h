#ifndef KEYBOARD_SCAN_H
#define KEYBOARD_SCAN_H
#include "keyboard_engine.h"
#include "optical_key.h"
typedef struct {
    keyboard_engine_t engine;
    optical_key_state_t keys[65];
    uint32_t sums[65];
    uint16_t raw[65], lower[65], upper[65];
    uint8_t levels[65], settling, calibrated, count;
    bool ready, valid;
} keyboard_scan_t;
typedef void (*keyboard_scan_event_t)(uint8_t key, bool down, uint8_t level);
void keyboard_scan_init(keyboard_scan_t *s, uint8_t profile);
/* Payloads of mode 6/A4 and mode 8/A4: three bytes per raw sensor. */
void keyboard_scan_frame(keyboard_scan_t *s, const uint16_t *raw,
                         const uint8_t *lower, const uint8_t *upper, keyboard_scan_event_t event);
bool keyboard_scan_neutral(const keyboard_scan_t *s);
/* Default-template thresholds, production special keys and editor preview. */
void keyboard_scan_thresholds(const keyboard_config_t *s, uint8_t key, optical_key_config_t *c);
#endif
