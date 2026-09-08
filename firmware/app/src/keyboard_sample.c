#include "keyboard_sample.h"
bool keyboard_sample_normalize(uint16_t native, uint16_t released_full_scale,
                               uint16_t pressed_full_scale, uint16_t *canonical)
{
    if(!canonical || released_full_scale==pressed_full_scale) return false;
    uint32_t travel,span;
    if(released_full_scale>pressed_full_scale) {
        span=released_full_scale-pressed_full_scale;
        travel=native>=released_full_scale ? 0u : native<=pressed_full_scale ? span :
            (uint32_t)released_full_scale-native;
    } else {
        span=pressed_full_scale-released_full_scale;
        travel=native<=released_full_scale ? 0u : native>=pressed_full_scale ? span :
            (uint32_t)native-released_full_scale;
    }
    *canonical=4096u-(travel*4095u+span/2u)/span;
    return true;
}
