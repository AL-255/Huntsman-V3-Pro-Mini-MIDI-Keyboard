#include "midi_music.h"

#define N(n) (1u<<(n))
#define SEVEN(a,b,c,d,e,f,g) (N(a)|N(b)|N(c)|N(d)|N(e)|N(f)|N(g))
#define FIVE(a,b,c,d,e) (N(a)|N(b)|N(c)|N(d)|N(e))
const midi_scale_t midi_scales[MIDI_SCALE_COUNT] = {
    [MIDI_SCALE_MAJOR] = {"MAJOR", SEVEN(0,2,4,5,7,9,11), 'J'},
    [MIDI_SCALE_MINOR] = {"MINOR", SEVEN(0,2,3,5,7,8,10), 'I'},
    [MIDI_SCALE_DORIAN] = {"DORIAN", SEVEN(0,2,3,5,7,9,10), 'D'},
    [MIDI_SCALE_PHRYGIAN] = {"PHRYGIAN", SEVEN(0,1,3,5,7,8,10), 'H'},
    [MIDI_SCALE_LYDIAN] = {"LYDIAN", SEVEN(0,2,4,6,7,9,11), 'Y'},
    [MIDI_SCALE_MIXOLYDIAN] = {"MIXOLYDIAN", SEVEN(0,2,4,5,7,9,10), 'M'},
    [MIDI_SCALE_LOCRIAN] = {"LOCRIAN", SEVEN(0,1,3,5,6,8,10), 'L'},
    [MIDI_SCALE_MAJOR_PENTATONIC] = {"MAJOR-PENTATONIC", FIVE(0,2,4,7,9), 'P'},
    [MIDI_SCALE_MINOR_PENTATONIC] = {"MINOR-PENTATONIC", FIVE(0,3,5,7,10), 'O'},
    [MIDI_SCALE_CHROMATIC] = {"CHROMATIC", 0x0fff, 'T'},
};
const char *const midi_root_names[12] = {
    "C","C-SHARP","D","D-SHARP","E","F","F-SHARP","G","G-SHARP","A","A-SHARP","B"
};
/* Two upper piano octaves, fixed physical selectors even if notes are muted
 * or GUI-remapped. Root means pitch class, independent of octave offset. */
const midi_root_key_t midi_root_keys[MIDI_ROOT_KEY_COUNT] = {
    {0x2b,0},{0x1e,1},{0x14,2},{0x1f,3},{0x1a,4},{0x08,5},
    {0x21,6},{0x15,7},{0x22,8},{0x17,9},{0x23,10},{0x1c,11},
    {0x18,0},{0x25,1},{0x0c,2},{0x26,3},{0x12,4},{0x13,5},
    {0x2d,6},{0x2f,7},{0x2e,8},{0x30,9},{0x2a,10},{0x31,11}
};

bool midi_music_contains(const midi_music_config_t *c, unsigned note)
{
    return c && c->root<12 && c->scale<MIDI_SCALE_COUNT && note<128 &&
        (midi_scales[c->scale].intervals & N((note+12u-c->root)%12u));
}
int midi_music_root_selector(unsigned usage)
{
    for(unsigned i=0;i<MIDI_ROOT_KEY_COUNT;++i)
        if(midi_root_keys[i].usage==usage) return midi_root_keys[i].pitch_class;
    return -1;
}
int midi_music_scale_selector(unsigned usage)
{
    for(unsigned i=0;i<MIDI_SCALE_COUNT;++i)
        if((unsigned)(midi_scales[i].selector-'A'+4)==usage) return (int)i;
    return -1;
}
