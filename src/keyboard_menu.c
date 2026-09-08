#include "keyboard_menu.h"
#include "keyboard_layout.h"
#include "keyboard_scan.h"
#include <string.h>

/* Brightness steps observed at original 0x2001b49c, not a gamma guess. */
static const uint8_t brightness_steps[20] = {
    0,3,6,10,15,21,27,36,45,56,68,81,96,112,128,144,172,194,224,255
};

enum { OPTION_KEYBOARD=1, OPTION_MIDI=2, OPTION_BOTH=3 };
typedef struct { uint8_t usage, modifier, modes; const char *word; } menu_option_t;
/* One table owns selector lookup, availability and Fn hints. Dynamic words
 * (mode and lower rows) are resolved from the current MIDI state. */
static const menu_option_t options[MENU_OPTION_COUNT] = {
    [MENU_CALIBRATION-1]={0x06,0,OPTION_KEYBOARD,"CALIBRATION"},
    [MENU_TRIGGER-1]={0x2b,0,OPTION_KEYBOARD,"TRIGGER"},
    [MENU_MODE-1]={0x28,0,OPTION_BOTH,NULL},
    [MENU_LIGHT_DOWN-1]={0x0e,0,OPTION_BOTH,"LIGHT-"},
    [MENU_LIGHT_UP-1]={0x0f,0,OPTION_BOTH,"LIGHT+"},
    [MENU_RAPID-1]={0x39,0,OPTION_KEYBOARD,"RAPID"},
    [MENU_RESET-1]={0x15,0,OPTION_BOTH,"RESET"},
    [MENU_LOWER-1]={0,2,OPTION_MIDI,NULL}, /* Left Shift */
    [MENU_KEY-1]={0x08,0,OPTION_MIDI,"KEY"},
    [MENU_SCALE-1]={0x16,0,OPTION_MIDI,"SCALE"},
};
_Static_assert(MENU_OPTION_COUNT<=16,"menu edge bitmap too small");

void keyboard_menu_init(keyboard_menu_t *s)
{
    memset(s,0,sizeof(*s));
    s->brightness=19;
    s->fn=s->tab=s->c=s->enter=s->k=s->l=s->caps=s->r=s->y=s->n=s->s=s->e=s->shift=255;
    memset(s->option_sensors,255,sizeof(s->option_sensors));
    s->choice_sensor=255;
}

void keyboard_menu_cancel(keyboard_menu_t *s)
{
    s->pending=MENU_NONE;
    s->brightness_session=false;
    s->reset_confirmation=s->confirmation_ready=false;
    s->music_page=MENU_NONE; s->choice_ready=false; s->choice_sensor=255;
    keyboard_text_stop(&s->text);
}

uint8_t keyboard_menu_control(uint8_t profile, uint8_t key)
{
    const keyboard_action_t *a=keyboard_action(profile,key,0);
    if (!a || a->type!=2u) return MENU_NONE;
    for(unsigned i=0;i<MENU_OPTION_COUNT;++i)
        if((options[i].modes & OPTION_KEYBOARD) && a->arg0==options[i].modifier && a->arg1==options[i].usage)
            return i+1u;
    return MENU_NONE;
}

uint8_t keyboard_menu_brightness(const keyboard_menu_t *s)
{
    return brightness_steps[s->brightness < 20u ? s->brightness : 19u];
}

static void layout(keyboard_menu_t *s, const keyboard_raw_t *raw)
{
    s->profile=raw->profile;
    s->fn=s->tab=s->c=s->enter=s->k=s->l=s->caps=s->r=s->y=s->n=s->s=s->e=s->shift=255;
    memset(s->option_sensors,255,sizeof(s->option_sensors));
    for (unsigned i=0; i<raw->count; ++i) {
        uint8_t key=keyboard_key_for_sensor(raw->profile,i);
        s->keys[i]=key;
        if (key==KEY_ID_FN) s->fn=i;
        const keyboard_action_t *a=keyboard_action(raw->profile,key,0);
        if (!a || a->type!=2u) continue;
        for(unsigned j=0;j<MENU_OPTION_COUNT;++j)
            if(a->arg0==options[j].modifier && a->arg1==options[j].usage) s->option_sensors[j]=i;
        if(a->arg0==2) s->shift=i;
        if(a->arg0) continue;
        switch (a->arg1) {
            case 0x2b: s->tab=i; break;
            case 0x06: s->c=i; break;
            case 0x28: s->enter=i; break;
            case 0x0e: s->k=i; break;
            case 0x0f: s->l=i; break;
            case 0x39: s->caps=i; break;
            case 0x15: s->r=i; break;
            case 0x1c: s->y=i; break;
            case 0x11: s->n=i; break;
            case 0x16: s->s=i; break;
            case 0x08: s->e=i; break;
            default: break;
        }
    }
    s->previous=s->bar=0;
    keyboard_menu_cancel(s);
}

static bool down(const keyboard_raw_t *raw, unsigned sensor)
{
    return sensor<raw->count && raw->down[sensor];
}

static bool preview_control(uint8_t profile, uint8_t key)
{
    if ((key>=2u && key<=11u) || key==KEY_ID_FN ||
        key==0x4fu || key==0x53u || key==0x54u || key==0x59u) return true;
    return profile==3u ? key==0x19u || key==0x26u || key==0x27u || key==0x28u :
        key==0x39u || key==0x3eu || key==0x40u || key==0x81u;
}

bool keyboard_menu_thresholds(keyboard_raw_t *raw, const uint16_t *lower,
                              const uint16_t *upper)
{
    if (!raw->count) return false;
    for (unsigned i=0; i<raw->count; ++i)
        if (!lower[i] || upper[i]>4096u || upper[i]<lower[i]+512u) return false;
    keyboard_config_t config=raw->engine.config;
    config.mode=KEY_CONFIG_NORMAL; /* committed, not editor-preview exclusions */
    for (unsigned i=0; i<raw->count; ++i) {
        optical_key_config_t c;
        keyboard_scan_thresholds(&config,keyboard_key_for_sensor(raw->profile,i),&c);
        unsigned span=upper[i]-lower[i];
        /* Exact inverse of floor((upper-raw)*256/span): production uses
         * level > press and level < release; raw uses < press and > release. */
        raw->press[i]=upper[i]-((c.press+1u)*span+255u)/256u+1u;
        raw->release[i]=upper[i]-(c.release*span+255u)/256u;
    }
    ++raw->revision;
    const keyboard_config_t after=raw->engine.config;
    keyboard_raw_invalidate(raw); /* cancel velocity fits and require neutral */
    raw->engine.config=after;
    raw->engine.config.fn=0;
    return true;
}

static int music_choice(const keyboard_menu_t *s, unsigned sensor)
{
    const keyboard_action_t *a=keyboard_action(s->profile,s->keys[sensor],0);
    if(!a || a->type!=2u || a->arg0) return -1;
    return s->music_page==MENU_KEY ? midi_music_root_selector(a->arg1) : midi_music_scale_selector(a->arg1);
}

static uint8_t music_page_frame(keyboard_menu_t *s, keyboard_raw_t *raw, uint32_t now)
{
    if(!raw->midi_mode || raw->revision!=s->pending_revision) {
        keyboard_menu_cancel(s); keyboard_raw_invalidate(raw); return MENU_NONE;
    }
    const bool ready=s->choice_ready;
    if(raw->armed) s->choice_ready=true; /* all keys released after page entry */
    keyboard_raw_invalidate(raw); /* menu input never reaches HID/MIDI */
    if(!ready) return MENU_NONE;
    unsigned held=0, sensor=0;
    for(unsigned i=0;i<raw->count;++i) if(raw->raw[i]<raw->press[i]) {
        if(s->keys[i]==KEY_ID_ESC) { keyboard_menu_cancel(s); return MENU_NONE; }
        ++held; sensor=i;
    }
    if(s->choice_sensor<raw->count) {
        if(raw->raw[s->choice_sensor]<=raw->release[s->choice_sensor]) return MENU_NONE;
        const uint8_t action=s->music_page==MENU_KEY ? MENU_SELECT_KEY : MENU_SELECT_SCALE;
        keyboard_menu_cancel(s); /* selection value remains available to caller */
        return action;
    }
    if(held>1u) { s->choice_ready=false; return MENU_NONE; }
    if(!held) return MENU_NONE;
    int choice=music_choice(s,sensor);
    if(choice<0) return MENU_NONE;
    s->choice_sensor=sensor; s->selection=choice;
    keyboard_text_start(&s->text,s->profile,
        s->music_page==MENU_KEY ? midi_root_names[choice] : midi_scales[choice].name,now);
    return MENU_NONE;
}

uint8_t keyboard_menu_frame(keyboard_menu_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper,
                         const keyboard_config_t *before, uint32_t now, bool calibration, bool lower_muted,
                         const midi_music_config_t *music)
{
    if (raw->profile && raw->profile!=s->profile) layout(s,raw);
    /* Only an explicit dirty actuation commit changes raw thresholds. Faults
     * and GUI changes cancel the editor without applying pending values. */
    if (before->mode==KEY_CONFIG_ACTUATION && before->dirty &&
        raw->engine.config.revision!=before->revision)
        (void)keyboard_menu_thresholds(raw,lower,upper);
    const uint8_t *sensors=s->option_sensors;
    unsigned held=0;
    /* Menu edges retain their own Schmitt history when preview entry clears
     * raw.down[]. A still-held key must not become a fresh press afterward. */
    for (unsigned i=0;i<MENU_OPTION_COUNT;++i) {
        if(!(options[i].modes & (raw->midi_mode?OPTION_MIDI:OPTION_KEYBOARD))) continue;
        unsigned sensor=sensors[i];
        if (sensor<raw->count && (s->previous & (1u<<i) ?
            raw->raw[sensor]<=raw->release[sensor] :
            raw->raw[sensor]<raw->press[sensor])) held|=1u<<i;
    }
    unsigned edges=held & ~s->previous;
    s->previous=held;
    if (!raw->valid || !raw->enabled || calibration || raw->engine.config.mode) {
        keyboard_menu_cancel(s); return MENU_NONE;
    }
    if(s->music_page) return music_page_frame(s,raw,now);
    if (s->reset_confirmation) {
        if (raw->revision!=s->pending_revision) {
            keyboard_menu_cancel(s); keyboard_raw_invalidate(raw); return MENU_NONE;
        }
        bool yes=s->y<raw->count && raw->raw[s->y]<raw->press[s->y];
        bool no=s->n<raw->count && raw->raw[s->n]<raw->press[s->n];
        bool ready=s->confirmation_ready;
        /* Require all keys released once, then consume all confirmation input.
         * A Y held before entry cannot erase; simultaneous Y/N cancels. */
        if (raw->armed) s->confirmation_ready=true;
        keyboard_raw_invalidate(raw);
        if (ready && (yes || no)) {
            keyboard_menu_cancel(s);
            return no ? MENU_NONE : MENU_RESET;
        }
        return MENU_NONE;
    }
    if (s->brightness_session && raw->revision!=s->pending_revision) {
        keyboard_menu_cancel(s); return MENU_NONE;
    }
    bool fn_held=s->fn<raw->count && raw->raw[s->fn]<=raw->release[s->fn];
    if (s->pending) {
        if (raw->revision!=s->pending_revision) { keyboard_menu_cancel(s); return MENU_NONE; }
        /* A preview disarms output. Observe release from raw values so clearing
         * down[] cannot lose a chord held in its hysteresis band. */
        if (raw->raw[s->fn]<=raw->release[s->fn] &&
            raw->raw[s->pending_sensor]<=raw->release[s->pending_sensor]) return MENU_NONE;
        uint8_t action=s->pending;
        keyboard_menu_cancel(s);
        /* Only brightness may select another action without a neutral scan.
         * Keep host output disarmed, even between taps with Fn held. */
        s->brightness_session=fn_held &&
            (action==MENU_LIGHT_DOWN || action==MENU_LIGHT_UP);
        keyboard_raw_invalidate(raw); /* consume remainder until all neutral */
        if (action==MENU_RESET) {
            s->reset_confirmation=true;
            keyboard_text_start(&s->text,raw->profile,"RESET?",now);
            return MENU_NONE;
        }
        if(action==MENU_KEY || action==MENU_SCALE) {
            s->music_page=action;
            s->selection=music ? (action==MENU_KEY?music->root:music->scale) :
                action==MENU_KEY?0:MIDI_SCALE_CHROMATIC;
            return MENU_NONE;
        }
        if (action==MENU_LIGHT_DOWN && s->brightness) --s->brightness;
        if (action==MENU_LIGHT_UP && s->brightness<19) ++s->brightness;
        if (action==MENU_TRIGGER || action==MENU_RAPID) {
            raw->engine.config.fn=1;
            keyboard_config_event(&raw->engine.config,
                action==MENU_TRIGGER ? KEY_ID_TAB : KEY_ID_CAPS,true,true);
            raw->engine.config.fn=0;
        }
        return action;
    }
    if (!fn_held) s->brightness_session=false;
    if (!(raw->armed && down(raw,s->fn)) && !s->brightness_session) return MENU_NONE;
    if (!edges || (edges & (edges-1u))) return MENU_NONE;
    unsigned index=0;
    while (!(edges & (1u<<index))) ++index;
    uint8_t action=index+1u;
    if (!raw->armed && action!=MENU_LIGHT_DOWN && action!=MENU_LIGHT_UP) return MENU_NONE;
    if ((raw->midi_mode && (action==MENU_CALIBRATION || action==MENU_TRIGGER || action==MENU_RAPID)) ||
        (raw->engine.config.locked && (action==MENU_TRIGGER || action==MENU_RAPID))) return MENU_NONE;
    const char *name=action==MENU_MODE ? (raw->midi_mode ? "KEYBOARD" : "MIDI") :
        action==MENU_LOWER ? (lower_muted ? "LOWER-ON" : "LOWER-OFF") : options[index].word;
    keyboard_text_start(&s->text,raw->profile,name,now);
    if (action==MENU_MODE) keyboard_text_color(&s->text,0,raw->midi_mode?255:0,raw->midi_mode?0:255);
    s->pending=action; s->pending_sensor=sensors[index];
    s->pending_revision=raw->revision;
    keyboard_raw_invalidate(raw);
    return MENU_NONE;
}

static void color(uint8_t profile, unsigned sensor, uint8_t *frame,
                  uint8_t r, uint8_t g, uint8_t b)
{
    if (sensor>= (profile==3u?65u:60u+profile)) return;
    const lighting_channels_t *c=&g_lighting_channels[profile-1u][sensor];
    uint8_t *p=frame+c->controller*192u;
    p[c->red]=r; p[c->green]=g; p[c->blue]=b;
}

void keyboard_menu_lights(keyboard_menu_t *s, const keyboard_raw_t *raw,
                          const uint16_t *lower, const uint16_t *upper,
                          uint8_t *frame, uint32_t now, bool midi, bool calibration)
{
    if (!s->profile) return;
    if (calibration) return; /* calibration feedback stays visible at brightness zero */
    if (keyboard_text_render(&s->text,frame,now)) {
        if (s->reset_confirmation) {
            color(s->profile,s->y,frame,0,255,0);
            color(s->profile,s->n,frame,255,0,0);
        }
        if(s->music_page) for(unsigned i=0;i<raw->count;++i)
            if(s->keys[i]==KEY_ID_ESC) color(s->profile,i,frame,255,0,0);
        return;
    }
    if(s->music_page) {
        memset(frame,0,LIGHTING_FRAME_SIZE);
        for(unsigned i=0;i<raw->count;++i) {
            int choice=music_choice(s,i);
            if(choice>=0) {
                if(choice==s->selection) color(s->profile,i,frame,0,255,0);
                else color(s->profile,i,frame,77,77,77);
            }
            if(s->keys[i]==KEY_ID_ESC) color(s->profile,i,frame,255,0,0);
        }
        return;
    }
    const keyboard_config_t *cfg=&raw->engine.config;
    unsigned brightness=keyboard_menu_brightness(s);
    if (raw->valid && raw->armed && !midi && cfg->mode) {
        /* Original 0x2000a078: dim number row, white travel bar, green selected
         * actuation (amber for rapid). Original 0x2001505c smooths at 20 ms. */
        unsigned peak=0;
        for (unsigned i=0; i<raw->count; ++i) {
            uint8_t key=s->keys[i];
            if (preview_control(s->profile,key)) continue;
            unsigned level=optical_key_level(lower[i],upper[i],raw->raw[i]);
            if (level>peak) peak=level;
        }
        unsigned target=0;
        const uint16_t *levels=cfg->mode==KEY_CONFIG_ACTUATION ? g_actuation_levels : g_rapid_levels;
        for (unsigned i=1; i<=10; ++i) if ((levels[i]>>8u)<peak) target=i;
        if ((uint32_t)(now-s->bar_at)>=20u) {
            if (s->bar<target) ++s->bar;
            if (s->bar>target) --s->bar;
            s->bar_at=now;
        }
        memset(frame,0,LIGHTING_FRAME_SIZE);
        for (unsigned i=0; i<raw->count; ++i) {
            uint8_t key=s->keys[i];
            if (key>=2u && key<=11u) {
                uint8_t v=key-1u<=s->bar?255u:25u;
                color(s->profile,i,frame,v,v,v);
                unsigned selected=cfg->mode==KEY_CONFIG_ACTUATION?cfg->actuation:cfg->rapid;
                if (key-1u==selected) color(s->profile,i,frame,
                    cfg->mode==KEY_CONFIG_ACTUATION?0u:253u,
                    cfg->mode==KEY_CONFIG_ACTUATION?255u:134u,
                    cfg->mode==KEY_CONFIG_ACTUATION?0u:17u);
            }
            if (key==KEY_ID_ESC) color(s->profile,i,frame,255,0,0);
        }
        return; /* original editor hints override ordinary brightness */
    }
    s->bar=0; s->bar_at=now;
    if (raw->valid && ((raw->armed && down(raw,s->fn)) ||
        (s->brightness_session && s->fn<raw->count &&
         raw->raw[s->fn]<=raw->release[s->fn]))) {
        memset(frame,0,LIGHTING_FRAME_SIZE);
        if (!midi) {
            for (unsigned i=0;i<raw->count;++i)
                if (keyboard_shortcut_usage(s->profile,s->keys[i])) color(s->profile,i,frame,0,255,0);
        }
        for(unsigned i=0;i<MENU_OPTION_COUNT;++i)
            if(options[i].modes & (midi?OPTION_MIDI:OPTION_KEYBOARD))
                color(s->profile,s->option_sensors[i],frame,255,255,255);
        color(s->profile,s->enter,frame,0,midi?255:0,midi?0:255);
        if (brightness<25u) brightness=25u; /* keep brightness-up discoverable */
    }
    if (brightness!=255u)
        for (unsigned i=0; i<LIGHTING_FRAME_SIZE; ++i)
            frame[i]=(frame[i]*brightness+255u)>>8u;
}
