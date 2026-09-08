#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <string.h>

uint8_t lighting_travel_pwm(uint16_t raw, uint16_t lower, uint16_t upper)
{
    if (!raw || raw > 4096u || !lower || lower >= upper || upper > 4096u) return 0u;
    if (raw <= lower) return 255u;
    if (raw >= upper) return 0u;
    return ((uint32_t)(upper - raw) * 255u + (upper - lower) / 2u) / (upper - lower);
}

void lighting_travel_frame(uint8_t profile, const uint16_t *raw, const uint16_t *lower,
                          const uint16_t *upper, bool valid, uint8_t *frame)
{
    memset(frame, 0, LIGHTING_FRAME_SIZE);
    if (!valid || !keyboard_layout_count(profile)) return;
    const unsigned count = keyboard_layout_count(profile);
    for (unsigned i = 0; i < count; ++i)
    {
        /* Invert only valid optical travel; invalid data must remain dark.
         * The shared travel helper stays press-increasing for MIDI pressure. */
        const bool usable=raw[i] && raw[i]<=4096u && lower[i] &&
                          lower[i]<upper[i] && upper[i]<=4096u;
        const uint8_t pwm = usable ? 255u-lighting_travel_pwm(raw[i],lower[i],upper[i]) : 0u;
        keyboard_light_set(profile,i,frame,pwm,pwm,pwm);
    }
}
