#ifndef MIDI_MUSIC_H
#define MIDI_MUSIC_H
#include <stdbool.h>
#include <stdint.h>

/* Pure musical data/predicates: no board, USB, scan or GUI dependencies. */
enum { MIDI_SCALE_MAJOR, MIDI_SCALE_MINOR, MIDI_SCALE_DORIAN, MIDI_SCALE_PHRYGIAN,
       MIDI_SCALE_LYDIAN, MIDI_SCALE_MIXOLYDIAN, MIDI_SCALE_LOCRIAN,
       MIDI_SCALE_MAJOR_PENTATONIC, MIDI_SCALE_MINOR_PENTATONIC,
       MIDI_SCALE_CHROMATIC, MIDI_SCALE_COUNT };
typedef struct { uint8_t root, scale; } midi_music_config_t;
typedef struct {
    const char *name; /* keyboard-text-compatible action name */
    uint16_t intervals; /* bit n = semitone n above the root */
    char selector; /* physical letter, not current MIDI mapping */
} midi_scale_t;
typedef struct { uint8_t usage, pitch_class; } midi_root_key_t;
#define MIDI_ROOT_KEY_COUNT 24u
extern const midi_scale_t midi_scales[MIDI_SCALE_COUNT];
extern const midi_root_key_t midi_root_keys[MIDI_ROOT_KEY_COUNT];
extern const char *const midi_root_names[12];
bool midi_music_contains(const midi_music_config_t *config, unsigned note);
int midi_music_root_selector(unsigned hid_usage);
int midi_music_scale_selector(unsigned hid_usage);
#endif
