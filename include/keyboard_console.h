#ifndef KEYBOARD_CONSOLE_H
#define KEYBOARD_CONSOLE_H

#include "keyboard_engine.h"

typedef void (*keyboard_console_output_t)(const char *text);
typedef bool (*keyboard_console_command_t)(const char *line);
typedef struct {
    keyboard_engine_t test;
    char line[64];
    uint8_t length;
    bool discard;
    keyboard_console_output_t output;
    keyboard_console_command_t command;
} keyboard_console_t;

void keyboard_console_init(keyboard_console_t *console, keyboard_console_output_t output);
void keyboard_console_feed(keyboard_console_t *console, uint8_t byte);
void keyboard_console_overflow(keyboard_console_t *console);

#endif
