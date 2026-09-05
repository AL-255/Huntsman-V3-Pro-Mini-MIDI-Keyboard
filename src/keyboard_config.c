#include "keyboard_config.h"

#include <string.h>
#include "keyboard_layout.h"

static uint8_t valid_level(uint8_t value)
{
    return value >= 1u && value <= 10u ? value : 4u;
}

void keyboard_config_init(keyboard_config_t *state, uint8_t profile)
{
    memset(state, 0, sizeof(*state));
    state->profile = profile;
    state->actuation = state->saved_actuation = 4u;
    state->rapid = state->saved_rapid = 4u;
    state->rapid_enabled = 1u;
}

static void commit(keyboard_config_t *state)
{
    if (!state->dirty) return;
    if (state->mode == KEY_CONFIG_ACTUATION) state->saved_actuation = state->actuation;
    if (state->mode == KEY_CONFIG_RAPID) state->saved_rapid = state->rapid;
    state->dirty = 0u;
    ++state->revision;
    /* Deliberately RAM-only: production 0x2001a3bc also writes its profile
     * storage. That flash layout is not owned by this application image. */
}

bool keyboard_config_event(keyboard_config_t *state, uint8_t key, bool down, bool fn_at_press)
{
    /* Production action 0x11/1 -> 0x200141f8, including during an editor. */
    if (key == KEY_ID_FN)
    {
        state->fn = down;
        return true;
    }
    if (state->mode == KEY_CONFIG_NORMAL)
    {
        const keyboard_action_t *action = keyboard_action(state->profile, key, fn_at_press);
        if (action == NULL || action->type != 0x11u ||
            (action->arg0 != 0x70u && action->arg0 != 0x71u)) return false;
        /* Entry is action based; both the latched layer and live FN matter.
         * Production 0x2000f41c: press only, FN live, no locked profile. */
        if (down && state->fn && !state->locked)
        {
            state->mode = action->arg0 == 0x70u ? KEY_CONFIG_ACTUATION : KEY_CONFIG_RAPID;
            if (state->mode == KEY_CONFIG_ACTUATION)
                state->actuation = valid_level(state->saved_actuation);
            else
                state->rapid = valid_level(state->saved_rapid);
            state->dirty = 0u;
        }
        return true;
    }

    /* Production 0x200134fc consumes ordinary key events while editing.
     * Releasing FN does not leave the editor. Number-row keys select 1..10. */
    if (!down) return true;
    if (key >= 2u && key <= 0x0bu)
    {
        if (state->mode == KEY_CONFIG_ACTUATION) state->actuation = key - 1u;
        else state->rapid = key - 1u;
        state->dirty = 1u;
        return true;
    }
    if (key == KEY_ID_ESC)
    {
        commit(state);
        state->mode = KEY_CONFIG_NORMAL;
        return true;
    }
    if (key == KEY_ID_TAB)
    {
        if (!fn_at_press) return true;
        const uint8_t previous = state->mode;
        commit(state);
        state->mode = previous == KEY_CONFIG_ACTUATION ? KEY_CONFIG_NORMAL : KEY_CONFIG_ACTUATION;
        if (state->mode) state->actuation = valid_level(state->saved_actuation);
        return true;
    }
    if (key == KEY_ID_CAPS)
    {
        if (!fn_at_press)
        {
            if (state->mode == KEY_CONFIG_RAPID && !state->locked)
            {
                state->rapid_enabled ^= 1u;
                state->saved_rapid = state->rapid;
                ++state->revision;
            }
            return true;
        }
        if (state->locked) return true;
        const uint8_t previous = state->mode;
        commit(state);
        state->mode = previous == KEY_CONFIG_RAPID ? KEY_CONFIG_NORMAL : KEY_CONFIG_RAPID;
        if (state->mode) state->rapid = valid_level(state->saved_rapid);
        return true;
    }

    int direction = 0;
    switch (key)
    {
        case 0x53: case 0x59: direction = 1; break;
        case 0x4f: case 0x54: direction = -1; break;
        case 0x39: case 0x40: if (state->profile <= 2u) direction = 1; break;
        case 0x3e: case 0x81: if (state->profile <= 2u) direction = -1; break;
        case 0x19: case 0x28: if (state->profile == 3u) direction = 1; break;
        case 0x26: case 0x27: if (state->profile == 3u) direction = -1; break;
        default: break;
    }
    if (direction)
    {
        uint8_t *level = state->mode == KEY_CONFIG_ACTUATION ? &state->actuation : &state->rapid;
        if (direction > 0 && *level < 10u) ++*level;
        if (direction < 0 && *level > 1u) --*level;
        state->dirty = 1u; /* The original marks dirty even at a bound. */
    }
    return true;
}

uint16_t keyboard_config_actuation_q16(const keyboard_config_t *state)
{
    return g_actuation_levels[valid_level(state->mode == KEY_CONFIG_ACTUATION ?
                                        state->actuation : state->saved_actuation)];
}

uint16_t keyboard_config_release_q16(const keyboard_config_t *state)
{
    const uint16_t press = keyboard_config_actuation_q16(state);
    return press <= 0x0766u ? 0x0100u : press - 0x0666u;
}

uint16_t keyboard_config_rapid_q16(const keyboard_config_t *state)
{
    return g_rapid_levels[valid_level(state->mode == KEY_CONFIG_RAPID ? state->rapid : state->saved_rapid)];
}
