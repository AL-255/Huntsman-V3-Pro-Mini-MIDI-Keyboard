#ifndef MIDI_TYPIST_KEYBOARD_SAMPLE_H
#define MIDI_TYPIST_KEYBOARD_SAMPLE_H
#include <stdbool.h>
#include <stdint.h>
/* Convert a board's ADC full-scale endpoints, not calibrated travel bounds.
 * Handles ascending/descending 16-bit ADCs. Clips out-of-range values.
 * Equal endpoints are an invalid board configuration, not a released key. */
bool keyboard_sample_normalize(uint16_t native, uint16_t released_full_scale,
                               uint16_t pressed_full_scale, uint16_t *canonical);
#endif
