#include "keyboard.h"

#include <string.h>
#include "keyboard_layout.h"

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
    /* Compatibility helper for ANSI only. New code uses the discovered
     * profile, key ID and action directly (FN is not a HID usage). */
    if (sensor_index >= 61u) return 0u;
    const keyboard_action_t *action = keyboard_action(1u,
        keyboard_key_for_sensor(1u, (uint8_t)sensor_index), 0u);
    if (action == NULL || action->type != 2u) return 0u;
    if (action->arg0)
        for (unsigned bit = 0; bit < 8u; ++bit)
            if (action->arg0 == (1u << bit)) return 0xe0u + bit;
    return action->arg1;
}
