#include "keyboard_midi.h"
#include "keyboard_layout.h"
#include "travel_lighting.h"
#include <string.h>

enum { ROLE_NOTE, ROLE_FN, ROLE_ENTER, ROLE_DOWN, ROLE_UP,
       ROLE_MODULATION, ROLE_BEND_DOWN, ROLE_BEND_UP, ROLE_SUSTAIN };

static void clear_voices(keyboard_midi_t *s)
{
    memset(s->active, 255, sizeof(s->active));
    memset(s->pending, 255, sizeof(s->pending));
    memset(s->current, 255, sizeof(s->current));
    memset(s->released, 0, sizeof(s->released));
    memset(s->previous, 0, sizeof(s->previous));
    memset(s->refs, 0, sizeof(s->refs));
    memset(s->pressure, 0, sizeof(s->pressure));
    memset(s->sent_pressure, 255, sizeof(s->sent_pressure));
    s->head = s->count = 0;
    s->was_armed = false;
    s->bend=8192; s->modulation=0; s->wheel_sweep=0;
    s->sustain=false;
}

void keyboard_midi_init(keyboard_midi_t *s)
{
    memset(s, 0, sizeof(*s));
    memset(s->mapping, 255, sizeof(s->mapping));
    clear_voices(s);
    s->sent_bend=8192;
    s->music.scale=MIDI_SCALE_CHROMATIC;
}

void keyboard_midi_abort(keyboard_midi_t *s)
{
    clear_voices(s);
    /* Sustain off first, individual Note Offs, All Sound Off/All Notes Off.
     * Also covers an IN packet already accepted before reset/mode change.
     * Never restart an in-progress sweep on repeated invalid frames. */
    if (!s->panic) s->panic = MIDI_CLEANUP_EVENTS;
}

void keyboard_midi_guard(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (s->was_armed && !raw->armed) keyboard_midi_abort(s);
}

void keyboard_midi_toggle(keyboard_midi_t *s, keyboard_raw_t *raw, uint32_t now)
{
    s->mode ^= 1u;
    raw->midi_mode=s->mode!=0;
    ++s->changes; s->changed_at=now;
    keyboard_midi_abort(s);
    keyboard_raw_invalidate(raw);
}

static uint8_t default_note(uint8_t usage)
{
    /* Scientific note names: C4=60, C5=72, C6=84. Two playable rows. */
    static const uint8_t map[][2] = {
        {0x2b,72},{0x14,74},{0x1a,76},{0x08,77},{0x15,79},{0x17,81},
        {0x1c,83},{0x18,84},{0x0c,86},{0x12,88},{0x13,89},{0x2f,91},
        {0x30,93},{0x31,95},{0x1e,73},{0x1f,75},{0x21,78},{0x22,80},
        {0x23,82},{0x25,85},{0x26,87},{0x2d,90},{0x2e,92},{0x2a,94},
        {0x04,61},{0x1d,62},{0x16,63},{0x1b,64},{0x06,65},{0x09,66},
        {0x19,67},{0x0a,68},{0x05,69},{0x0b,70},{0x11,71},{0x10,72},
        {0x0e,73},{0x36,74},{0x0f,75},{0x37,76},{0x38,77},{0x34,78}
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (map[i][0] == usage) return map[i][1];
    return MIDI_UNMAPPED;
}

void keyboard_midi_toggle_lower(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (!s->mode) return;
    s->lower_muted=!s->lower_muted;
    keyboard_midi_abort(s); /* includes pending strikes and shared-pitch owners */
    keyboard_raw_invalidate(raw); /* all keys neutral before new note edges */
}

static bool note_enabled(const keyboard_midi_t *s, unsigned sensor)
{
    const int note=(int)s->mapping[sensor]+12*(int)s->octave;
    return s->mapping[sensor]!=MIDI_UNMAPPED && note>=0 &&
        midi_music_contains(&s->music,(unsigned)note) &&
        !(s->lower_muted && (s->lower_rows[sensor/8u] & (1u<<(sensor%8u))));
}

bool keyboard_midi_select_music(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned root, unsigned scale)
{
    if(!s->mode || root>=12u || scale>=MIDI_SCALE_COUNT) return false;
    s->music=(midi_music_config_t){root,scale};
    ++raw->revision;
    keyboard_midi_abort(s);
    keyboard_raw_invalidate(raw);
    return true;
}

static void layout(keyboard_midi_t *s, const keyboard_raw_t *raw)
{
    if (s->profile) keyboard_midi_abort(s);
    s->profile = raw->profile;
    memset(s->mapping, 255, sizeof(s->mapping));
    memset(s->role, 0, sizeof(s->role));
    memset(s->lower_rows,0,sizeof(s->lower_rows));
    for (unsigned i = 0; i < raw->count; ++i) {
        const uint8_t key = keyboard_key_for_sensor(raw->profile, i);
        /* Recovered physical IDs: Caps..Enter = 0x1e..0x2b,
         * Left Shift..Right Shift = 0x2c..0x39, including ISO/JIS extras.
         * Bottom-row Fn, octave and wheel controls are outside these ranges. */
        if (key>=KEY_ID_CAPS && key<=0x39u) s->lower_rows[i/8u]|=1u<<(i%8u);
        const keyboard_action_t *a = keyboard_action(raw->profile, key, 0);
        if (key == KEY_ID_FN) s->role[i] = ROLE_FN;
        else if (a && a->type == 2) {
            if (a->arg0 == 64) s->role[i] = ROLE_DOWN;
            else if (a->arg0 == 16) s->role[i] = ROLE_UP;
            else if (a->arg0 == 8) s->role[i] = ROLE_MODULATION;
            else if (a->arg0 == 1) s->role[i] = ROLE_BEND_DOWN;
            else if (a->arg0 == 4) s->role[i] = ROLE_BEND_UP;
            else if (a->arg1 == 0x2c) s->role[i] = ROLE_SUSTAIN;
            else if (a->arg1 == 0x28) s->role[i] = ROLE_ENTER;
            if (a->arg0 == 2) s->mapping[i] = 60; /* left Shift: C4 in MIDI only */
            else if (!a->arg0) s->mapping[i] = default_note(a->arg1);
        }
    }
}

static bool enqueue(keyboard_midi_t *s, uint8_t status, uint8_t note, uint8_t value)
{
    if (s->count == MIDI_QUEUE) {
        ++s->errors;
        keyboard_midi_abort(s); /* no silent loss of a required Note Off */
        return false;
    }
    uint8_t *p = s->queue[(s->head + s->count++) % MIDI_QUEUE];
    p[0] = status; p[1] = note; p[2] = value;
    return true;
}

static bool note_off(keyboard_midi_t *s, uint8_t note)
{
    if (!s->refs[note] || --s->refs[note]) return true;
    s->pressure[note] = 0;
    return enqueue(s, 0x80, note, 0);
}

bool keyboard_midi_map(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned sensor, unsigned note)
{
    if (sensor >= raw->count || !s->profile || (note > 127 && note != MIDI_UNMAPPED) ||
        s->role[sensor] == ROLE_FN || s->role[sensor] >= ROLE_DOWN)
        return false;
    keyboard_midi_abort(s);
    s->mapping[sensor] = note;
    ++raw->revision;
    keyboard_raw_invalidate(raw);
    return true;
}

static unsigned wheel_depth(uint16_t value)
{
    return value>=3800u ? 0u : value<=1000u ? 2800u : 3800u-value;
}

void keyboard_midi_frame(keyboard_midi_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper, uint32_t now)
{
    (void)now;
    if (raw->profile && s->profile != raw->profile) layout(s, raw);
    keyboard_midi_guard(s, raw);
    if (!raw->armed) return;
    s->was_armed=true;
    bool fn = false;
    int shift = 0;
    for (unsigned i = 0; i < raw->count; ++i) {
        if (s->role[i] == ROLE_FN && raw->down[i]) fn = true;
        if (raw->down[i] && !s->previous[i]) {
            if (s->role[i] == ROLE_UP) ++shift;
            if (s->role[i] == ROLE_DOWN) --shift;
        }
    }
    if (s->panic) {
        /* A host that does not consume MIDI must not trap the mode chord or
         * block normal HID. Presses during cleanup require a fresh edge. */
        memcpy(s->previous, raw->down, sizeof(s->previous));
        return;
    }
    if (s->mode && !fn && shift) {
        int octave = s->octave + shift;
        s->octave = octave < -10 ? -10 : octave > 10 ? 10 : octave;
    }
    if (s->mode) {
        bool sustain=false;
        for(unsigned i=0;i<raw->count;++i)
            if(s->role[i]==ROLE_SUSTAIN && raw->down[i] && !fn &&
               (s->sustain || !s->previous[i])) sustain=true;
        if(sustain!=s->sustain) {
            /* Ordered with note edges, never coalesced like analog wheels.
             * Same-scan pedal changes precede Note Off/On processing. */
            if(!enqueue(s,0xb0,64,sustain?127:0)) goto overflow;
            s->sustain=sustain;
        }
        int bend=0;
        s->modulation=0;
        if (!fn) for (unsigned i=0; i<raw->count; ++i) {
            unsigned depth=wheel_depth(raw->raw[i]);
            if (s->role[i]==ROLE_MODULATION) s->modulation=(depth*127u+1400u)/2800u;
            if (s->role[i]==ROLE_BEND_DOWN) bend-=(int)depth;
            if (s->role[i]==ROLE_BEND_UP) bend+=(int)depth;
        }
        /* Sum travel before quantization: equal opposing pressure is exactly
         * center despite MIDI's asymmetric negative/positive endpoint sizes. */
        s->bend=bend<0 ? 8192-((-bend*8192+1400)/2800) :
                         8192+((bend*8191+1400)/2800);
        memset(s->pressure, 0, sizeof(s->pressure));
        for (unsigned i = 0; i < raw->count; ++i) {
            if (s->previous[i] && !raw->down[i]) {
                if (s->current[i] < 5) s->released[i] |= 1u << s->current[i];
                if (s->active[i] != MIDI_UNMAPPED) {
                    if (!note_off(s, s->active[i])) goto overflow;
                    s->active[i] = MIDI_UNMAPPED;
                }
            }
            /* Phase slot completes exactly five hardware frames after trigger.
             * Short taps retain their delayed On+Off; overlapping fits use
             * independent slots and never borrow another key's velocity. */
            uint8_t note = s->pending[i][s->phase];
            if (note != MIDI_UNMAPPED) {
                unsigned velocity = (unsigned)(raw->velocity[i].value * 127.0f + 0.5f);
                if (!velocity) velocity = 1; /* Note On zero means Note Off */
                if (!s->refs[note]++ && !enqueue(s, 0x90, note, velocity)) goto overflow;
                s->sent_pressure[note] = 255;
                if (s->released[i] & (1u << s->phase)) {
                    if (!note_off(s, note)) goto overflow;
                } else s->active[i] = note;
                if (s->current[i] == s->phase) s->current[i] = 255;
                s->pending[i][s->phase] = MIDI_UNMAPPED;
            }
            if (!fn && raw->down[i] && !s->previous[i] && note_enabled(s,i)) {
                const int shifted = (int)s->mapping[i] + (int)s->octave * 12;
                /* Out-of-range notes are muted, never wrapped or clamped. */
                if (shifted >= 0 && shifted <= 127) {
                    s->pending[i][s->phase] = (uint8_t)shifted;
                    s->released[i] &= ~(1u << s->phase);
                    s->current[i] = s->phase;
                }
            }
            if (s->active[i] != MIDI_UNMAPPED) {
                const uint8_t pressure = ((unsigned)lighting_travel_pwm(raw->raw[i], lower[i], upper[i]) * 127u + 127u) / 255u;
                note = s->active[i];
                if (pressure > s->pressure[note]) s->pressure[note] = pressure;
            }
        }
    }
    memcpy(s->previous, raw->down, sizeof(s->previous));
    s->phase = (s->phase + 1u) % 5u;
    return;
overflow:
    keyboard_raw_invalidate(raw);
}

void keyboard_midi_service(keyboard_midi_t *s, uint32_t now, midi_send_fn send)
{
    if (s->panic) {
        unsigned index = MIDI_CLEANUP_EVENTS - s->panic;
        bool ok = index==0 ? send(11,0xb0,64,0) :
                  index<=128 ? send(8, 0x80, index-1u, 0) :
                  index<131 ? send(11, 0xb0, index == 129 ? 120 : 123, 0) :
                  index==131 ? send(11,0xb0,1,0) : send(14,0xe0,0,64);
        if (ok) {
            if (index==131) s->sent_modulation=0;
            if (index==132) s->sent_bend=8192;
            --s->panic;
        }
        return;
    }
    if (s->count) {
        const uint8_t *p = s->queue[s->head];
        if (send(p[0] >> 4u, p[0], p[1], p[2])) {
            s->head = (s->head + 1u) % MIDI_QUEUE;
            --s->count;
        }
        return;
    }
    /* Latest-value registers, not the note FIFO. Check both controllers once
     * per millisecond; a busy endpoint retains only their newest positions. */
    if (!s->wheel_sweep && (uint32_t)(now-s->wheel_at)>=1u) {
        s->wheel_sweep=3; s->wheel_at=now;
    }
    if (s->wheel_sweep & 1u) {
        if (s->modulation!=s->sent_modulation) {
            if (!send(11,0xb0,1,s->modulation)) return;
            s->sent_modulation=s->modulation;
        }
        s->wheel_sweep &= ~1u;
    }
    if (s->wheel_sweep & 2u) {
        if (s->bend!=s->sent_bend) {
            if (!send(14,0xe0,s->bend & 127u,s->bend>>7u)) return;
            s->sent_bend=s->bend;
        }
        s->wheel_sweep &= ~2u;
    }
    if (!s->pressure_sweep && (uint32_t)(now - s->pressure_at) >= 10u) {
        s->pressure_sweep = true; s->pressure_cursor = 0; s->pressure_at = now;
    }
    while (s->pressure_sweep) {
        const unsigned note = s->pressure_cursor;
        if (s->refs[note] && s->pressure[note] != s->sent_pressure[note]) {
            if (!send(10, 0xa0, note, s->pressure[note])) return;
            s->sent_pressure[note] = s->pressure[note];
        }
        if (++s->pressure_cursor == 128) s->pressure_sweep = false;
    }
}

void keyboard_midi_lights(const keyboard_midi_t *s, uint8_t *frame, uint32_t now)
{
    if (!s->profile) return;
    unsigned magnitude = s->octave < 0 ? -(int)s->octave : s->octave;
    if (magnitude > 10u) magnitude = 10u;
    /* Full period 1200 ms at +/-1, down to 120 ms at +/-10. Minimum
     * half-period 60 ms stays above the LED scheduler's 40 ms frame period. */
    const unsigned half_period = 60u * (11u - magnitude);
    const bool blink_on = (now / half_period) % 2u == 0u;
    const unsigned count = s->profile == 3 ? 65 : 60 + s->profile;
    for (unsigned i = 0; i < count; ++i) {
        const bool octave_key = s->mode &&
            ((s->octave < 0 && s->role[i] == ROLE_DOWN) ||
             (s->octave > 0 && s->role[i] == ROLE_UP));
        const lighting_channels_t *ch = &g_lighting_channels[s->profile - 1][i];
        uint8_t *p = frame + ch->controller * 192u;
        if (s->mode && !note_enabled(s,i))
            p[ch->red]=p[ch->green]=p[ch->blue]=0;
        /* Mode/octave hints remain explicit overlays, not note backlighting. */
        if (s->role[i] != ROLE_ENTER && !(s->mode && s->role[i]>=ROLE_DOWN)) continue;
        if (octave_key) {
            p[ch->red] = p[ch->green] = 0;
            p[ch->blue] = blink_on ? 255 : 0;
            continue;
        }
        p[ch->red] = 0;
        p[ch->green] = s->mode ? 0 : 255;
        p[ch->blue] = s->mode ? 255 : 0;
    }
}
