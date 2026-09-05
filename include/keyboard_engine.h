#ifndef KEYBOARD_ENGINE_H
#define KEYBOARD_ENGINE_H

#include "keyboard.h"
#include "keyboard_config.h"

typedef struct {
    keyboard_config_t config;
    keyboard_report_t report;
    uint8_t pressed[256], fn_at_press[256], modifiers[256], usages[256];
} keyboard_engine_t;

void keyboard_engine_init(keyboard_engine_t *engine, uint8_t profile);
/* Events use recovered production key IDs, not scan indices or HID usages. */
bool keyboard_engine_event(keyboard_engine_t *engine, uint8_t key, bool down);
void keyboard_engine_release_all(keyboard_engine_t *engine);

#endif
