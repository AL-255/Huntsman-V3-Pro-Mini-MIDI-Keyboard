#include "keyboard_engine.h"

#include <string.h>
#include "keyboard_layout.h"

void keyboard_engine_init(keyboard_engine_t *engine, uint8_t profile)
{
    memset(engine, 0, sizeof(*engine));
    keyboard_config_init(&engine->config, profile);
}

static void rebuild_report(keyboard_engine_t *engine)
{
    keyboard_report_clear(&engine->report);
    for (unsigned key = 1; key < 256u; ++key)
    {
        engine->report.modifiers |= engine->modifiers[key];
        (void)keyboard_report_set_usage(&engine->report, engine->usages[key], true);
    }
}

void keyboard_engine_release_all(keyboard_engine_t *engine)
{
    memset(engine->pressed, 0, sizeof(engine->pressed));
    memset(engine->fn_at_press, 0, sizeof(engine->fn_at_press));
    memset(engine->modifiers, 0, sizeof(engine->modifiers));
    memset(engine->usages, 0, sizeof(engine->usages));
    engine->config.fn = 0u;
    keyboard_report_clear(&engine->report);
}

bool keyboard_engine_event(keyboard_engine_t *engine, uint8_t key, bool down)
{
    if (key == 0u || engine->pressed[key] == down ||
        keyboard_action(engine->config.profile, key, 0u) == NULL) return false;
    engine->pressed[key] = down;
    if (down) engine->fn_at_press[key] = engine->config.fn;
    const bool fn = engine->fn_at_press[key] != 0u;
    const uint8_t mode = engine->config.mode;
    const bool consumed = keyboard_config_event(&engine->config, key, down, fn);
    engine->modifiers[key] = engine->usages[key] = 0u;
    if (!consumed && down)
    {
        const keyboard_action_t *action = keyboard_action(engine->config.profile, key, fn);
        if (action != NULL && action->type == 2u)
        {
            engine->modifiers[key] = action->arg0;
            engine->usages[key] = action->arg1;
        }
        /* Non-keyboard actions (media, profile, lighting) are not guessed. */
    }
    if (!mode && engine->config.mode)
    {
        /* Safety integration: no pre-editor report can leave a held host key.
         * Physical pressed bits remain latched until the actual release. */
        memset(engine->modifiers, 0, sizeof(engine->modifiers));
        memset(engine->usages, 0, sizeof(engine->usages));
    }
    if (key == KEY_ID_FN && !down)
    {
        /* Production 0x200141f8 releases held actions whose layers differ;
         * it does not press their normal-layer replacement. */
        for (unsigned id = 1; id < 256u; ++id)
        {
            if (!engine->pressed[id] || !engine->fn_at_press[id]) continue;
            const keyboard_action_t *base = keyboard_action(engine->config.profile, id, 0u);
            const keyboard_action_t *layer = keyboard_action(engine->config.profile, id, 1u);
            if (base != NULL && layer != NULL && memcmp(base, layer, sizeof(*base)))
                engine->modifiers[id] = engine->usages[id] = 0u;
        }
    }
    rebuild_report(engine);
    return true;
}
