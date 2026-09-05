#ifndef HUNTSMAN_KEYBOARD_H
#define HUNTSMAN_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYBOARD_NKRO_USAGE_MIN 0x04u
#define KEYBOARD_NKRO_USAGE_MAX 0x73u
#define KEYBOARD_NKRO_BITMAP_BYTES 14u
#define KEYBOARD_NKRO_REPORT_BYTES 16u

typedef struct
{
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[KEYBOARD_NKRO_BITMAP_BYTES];
} keyboard_report_t;

void keyboard_report_clear(keyboard_report_t *report);
bool keyboard_report_set_usage(keyboard_report_t *report, uint8_t usage, bool pressed);
bool keyboard_report_get_usage(const keyboard_report_t *report, uint8_t usage);
uint8_t keyboard_usage_for_sensor(size_t sensor_index);

#endif
