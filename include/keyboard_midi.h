#ifndef KEYBOARD_MIDI_H
#define KEYBOARD_MIDI_H
#include "keyboard_raw.h"
#define MIDI_UNMAPPED 255u
#define MIDI_QUEUE 128u
typedef bool (*midi_send_fn)(uint8_t, uint8_t, uint8_t, uint8_t);
typedef struct {
    uint8_t mapping[65], role[65], active[65], current[65], released[65];
    uint8_t pending[65][5];
    bool previous[65];
    uint8_t refs[128], pressure[128], sent_pressure[128];
    uint8_t queue[MIDI_QUEUE][3];
    uint16_t head, count, panic;
    uint8_t profile, mode, phase, pressure_cursor;
    int8_t octave;
    bool was_armed, pressure_sweep;
    uint32_t changes, changed_at, errors, pressure_at;
} keyboard_midi_t;
void keyboard_midi_init(keyboard_midi_t *s);
void keyboard_midi_abort(keyboard_midi_t *s);
void keyboard_midi_guard(keyboard_midi_t *s, keyboard_raw_t *raw);
void keyboard_midi_frame(keyboard_midi_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper, uint32_t now);
bool keyboard_midi_map(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned sensor, unsigned note);
void keyboard_midi_service(keyboard_midi_t *s, uint32_t now, midi_send_fn send);
void keyboard_midi_lights(const keyboard_midi_t *s, uint8_t *frame, uint32_t now);
#endif
