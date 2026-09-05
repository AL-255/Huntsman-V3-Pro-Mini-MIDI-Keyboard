#include "keyboard_midi.h"
#include "keyboard_layout.h"
#include "travel_lighting.h"
#include <string.h>

enum { ROLE_NOTE, ROLE_FN, ROLE_ENTER, ROLE_DOWN, ROLE_UP };

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
}

void keyboard_midi_init(keyboard_midi_t *s)
{
    memset(s, 0, sizeof(*s));
    memset(s->mapping, 255, sizeof(s->mapping));
    clear_voices(s);
}

void keyboard_midi_abort(keyboard_midi_t *s)
{
    clear_voices(s);
    /* Individual Note Offs, All Sound Off, All Notes Off, on channel 1.
     * Also covers an IN packet already accepted before reset/mode change.
     * Never restart an in-progress sweep on repeated invalid frames. */
    if (!s->panic) s->panic = 130;
}

void keyboard_midi_guard(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (s->was_armed && !raw->armed) keyboard_midi_abort(s);
}

static uint8_t default_note(uint8_t usage)
{
    /* Scientific note names: C0=12, C4=60. User's octave jumps intentional. */
    static const uint8_t map[][2] = {
        {0x2b,12},{0x14,14},{0x1a,16},{0x08,17},{0x15,19},{0x17,33},
        {0x1c,35},{0x18,24},{0x0c,26},{0x12,28},{0x13,29},{0x2f,31},
        {0x30,45},{0x31,47},{0x1e,13},{0x1f,15},{0x21,18},{0x22,32},
        {0x23,34},{0x25,25},{0x26,27},{0x2d,30},{0x2e,44},{0x2a,46}
    };
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (map[i][0] == usage) return map[i][1];
    return MIDI_UNMAPPED;
}

static void layout(keyboard_midi_t *s, const keyboard_raw_t *raw)
{
    if (s->profile) keyboard_midi_abort(s);
    s->profile = raw->profile;
    memset(s->mapping, 255, sizeof(s->mapping));
    memset(s->role, 0, sizeof(s->role));
    for (unsigned i = 0; i < raw->count; ++i) {
        const uint8_t key = keyboard_key_for_sensor(raw->profile, i);
        const keyboard_action_t *a = keyboard_action(raw->profile, key, 0);
        if (key == KEY_ID_FN) s->role[i] = ROLE_FN;
        else if (a && a->type == 2) {
            if (a->arg0 == 1) s->role[i] = ROLE_DOWN;
            else if (a->arg0 == 4) s->role[i] = ROLE_UP;
            else if (a->arg1 == 0x28) s->role[i] = ROLE_ENTER;
            if (!a->arg0) s->mapping[i] = default_note(a->arg1);
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
        s->role[sensor] == ROLE_FN || s->role[sensor] == ROLE_DOWN || s->role[sensor] == ROLE_UP)
        return false;
    keyboard_midi_abort(s);
    s->mapping[sensor] = note;
    ++raw->revision;
    keyboard_raw_invalidate(raw);
    return true;
}

void keyboard_midi_frame(keyboard_midi_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper, uint32_t now)
{
    if (raw->profile && s->profile != raw->profile) layout(s, raw);
    keyboard_midi_guard(s, raw);
    if (!raw->armed) return;
    s->was_armed = true;
    bool fn = false, enter = false;
    int shift = 0;
    for (unsigned i = 0; i < raw->count; ++i) {
        if (s->role[i] == ROLE_FN && raw->down[i]) fn = true;
        if (s->role[i] == ROLE_ENTER && raw->down[i]) enter = true;
        if (raw->down[i] && !s->previous[i]) {
            if (s->role[i] == ROLE_UP) ++shift;
            if (s->role[i] == ROLE_DOWN) --shift;
        }
    }
    if (fn && enter) {
        s->mode ^= 1u;
        raw->midi_mode = s->mode != 0;
        ++s->changes; s->changed_at = now;
        keyboard_midi_abort(s);
        keyboard_raw_invalidate(raw); /* consumes chord; requires all released */
        return;
    }
    if (s->panic) {
        /* A host that does not consume MIDI must not trap the mode chord or
         * block normal HID. Presses during cleanup require a fresh edge. */
        memcpy(s->previous, raw->down, sizeof(s->previous));
        return;
    }
    if (s->mode && shift) {
        int octave = s->octave + shift;
        s->octave = octave < -10 ? -10 : octave > 10 ? 10 : octave;
    }
    if (s->mode) {
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
            if (raw->down[i] && !s->previous[i] && s->mapping[i] != MIDI_UNMAPPED) {
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
        unsigned index = 130u - s->panic;
        bool ok = index < 128 ? send(8, 0x80, index, 0) : send(11, 0xb0, index == 128 ? 120 : 123, 0);
        if (ok) --s->panic;
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
    const uint32_t elapsed = now - s->changed_at;
    const bool flash = s->changes && elapsed < 600u && (elapsed / 150u) % 2u == 0u;
    const unsigned count = s->profile == 3 ? 65 : 60 + s->profile;
    for (unsigned i = 0; i < count; ++i) {
        if (!flash && s->role[i] != ROLE_ENTER) continue;
        const lighting_channels_t *ch = &g_lighting_channels[s->profile - 1][i];
        uint8_t *p = frame + ch->controller * 192u;
        p[ch->red] = 0;
        p[ch->green] = s->mode ? 0 : flash ? 128 : 24;
        p[ch->blue] = s->mode ? (flash ? 128 : 24) : 0;
    }
}
