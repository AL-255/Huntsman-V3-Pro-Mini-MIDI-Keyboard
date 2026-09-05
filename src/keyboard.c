#include "keyboard.h"

#include <string.h>

/*
 * Provisional physical sensor order. The optical ASIC's runtime map is not
 * present in the application image, so this table is intentionally isolated
 * for calibration on hardware. It follows a conventional ANSI 60% row order.
 */
static const uint8_t s_sensor_usage[] = {
    0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a,
    0x2b, 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x31,
    0x39, 0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x28,
    0xe1, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5,
    0xe0, 0xe3, 0xe2, 0x2c, 0xe6, 0x00, 0x65, 0xe4,
};

void keyboard_report_clear(keyboard_report_t *report)
{
    memset(report, 0, sizeof(*report));
}

bool keyboard_report_set_usage(keyboard_report_t *report, uint8_t usage, bool pressed)
{
    if ((usage >= 0xe0u) && (usage <= 0xe7u))
    {
        const uint8_t bit = (uint8_t)(1u << (usage - 0xe0u));
        if (pressed)
        {
            report->modifiers |= bit;
        }
        else
        {
            report->modifiers &= (uint8_t)~bit;
        }
        return true;
    }

    if ((usage < KEYBOARD_NKRO_USAGE_MIN) || (usage > KEYBOARD_NKRO_USAGE_MAX))
    {
        return false;
    }

    const uint8_t index = (uint8_t)(usage - KEYBOARD_NKRO_USAGE_MIN);
    const uint8_t bit = (uint8_t)(1u << (index & 7u));
    if (pressed)
    {
        report->keys[index >> 3u] |= bit;
    }
    else
    {
        report->keys[index >> 3u] &= (uint8_t)~bit;
    }
    return true;
}

bool keyboard_report_get_usage(const keyboard_report_t *report, uint8_t usage)
{
    if ((usage >= 0xe0u) && (usage <= 0xe7u))
    {
        return (report->modifiers & (uint8_t)(1u << (usage - 0xe0u))) != 0u;
    }
    if ((usage < KEYBOARD_NKRO_USAGE_MIN) || (usage > KEYBOARD_NKRO_USAGE_MAX))
    {
        return false;
    }
    const uint8_t index = (uint8_t)(usage - KEYBOARD_NKRO_USAGE_MIN);
    return (report->keys[index >> 3u] & (uint8_t)(1u << (index & 7u))) != 0u;
}

uint8_t keyboard_usage_for_sensor(size_t sensor_index)
{
    return (sensor_index < (sizeof(s_sensor_usage) / sizeof(s_sensor_usage[0])))
               ? s_sensor_usage[sensor_index]
               : 0u;
}
