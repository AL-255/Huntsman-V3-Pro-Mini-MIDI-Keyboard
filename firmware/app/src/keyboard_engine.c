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

uint8_t keyboard_shortcut_usage(uint8_t profile, uint8_t key)
{
    const keyboard_action_t *base=keyboard_action(profile,key,0);
    if (!base || base->type!=2 || base->arg0) return 0;
    if (base->arg1>=0x1e && base->arg1<=0x27) return 0x3a+base->arg1-0x1e;
    switch (base->arg1) {
        case 0x29: return 0x35; /* Esc -> grave/backtick */
        case 0x2d: return 0x44; /* - -> F11 */
        case 0x2e: return 0x45; /* = -> F12 */
        case 0x2a: return 0x4c; /* Backspace -> Delete */
        case 0x1c: return 0x49; /* Y -> Insert */
        case 0x13: return 0x46; /* P -> Print Screen */
        case 0x11: return 0x4d; /* N -> End */
        case 0x10: return 0x4e; /* M -> Page Down */
        case 0x0b: return 0x4a; /* H -> Home */
        case 0x0d: return 0x4b; /* J -> Page Up */
        default: return 0;
    }
}

static void report_action(uint8_t profile, uint8_t key, bool fn, bool application,
                          uint8_t *modifier, uint8_t *usage)
{
    *modifier=*usage=0;
    if (application) {
        if (fn && (*usage=keyboard_shortcut_usage(profile,key))) return;
        const keyboard_action_t *base=keyboard_action(profile,key,0);
        if (base && base->type==2 && keyboard_layout(profile)->compact_navigation) {
            if (base->arg0==64) *usage=0x50; /* Right Alt -> Left */
            else if (base->arg0==16) *usage=0x4f; /* Right Ctrl -> Right */
            else if (base->arg0==32) *usage=0x52; /* Right Shift -> Up */
            else if (!base->arg0 && base->arg1==0x65) *usage=0x51; /* Menu -> Down */
            if (*usage) return;
        }
    }
    const keyboard_action_t *action=keyboard_action(profile,key,fn);
    if (action && action->type==2) { *modifier=action->arg0; *usage=action->arg1; }
}

static bool engine_event(keyboard_engine_t *engine, uint8_t key, bool down, bool application)
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
        report_action(engine->config.profile,key,fn,application && !mode,
                      &engine->modifiers[key],&engine->usages[key]);
        /* Non-keyboard actions (media, profile, lighting) are not guessed. */
    }
    if (!mode && engine->config.mode)
    {
        /* Safety integration: no pre-editor report can leave a held host key.
         * Physical pressed bits remain latched until the actual release. */
        memset(engine->modifiers, 0, sizeof(engine->modifiers));
        memset(engine->usages, 0, sizeof(engine->usages));
    }
    if (key == keyboard_layout(engine->config.profile)->fn && !down)
    {
        /* Production 0x200141f8 releases held actions whose layers differ;
         * it does not press their normal-layer replacement. */
        for (unsigned id = 1; id < 256u; ++id)
        {
            if (!engine->pressed[id] || !engine->fn_at_press[id]) continue;
            uint8_t base_mod,base_usage,layer_mod,layer_usage;
            report_action(engine->config.profile,id,false,application,&base_mod,&base_usage);
            report_action(engine->config.profile,id,true,application,&layer_mod,&layer_usage);
            if (base_mod!=layer_mod || base_usage!=layer_usage)
                engine->modifiers[id] = engine->usages[id] = 0u;
        }
    }
    rebuild_report(engine);
    return true;
}

bool keyboard_engine_event(keyboard_engine_t *engine, uint8_t key, bool down)
{
    return engine_event(engine,key,down,false);
}

bool keyboard_application_event(keyboard_engine_t *engine, uint8_t key, bool down)
{
    return engine_event(engine,key,down,true);
}
