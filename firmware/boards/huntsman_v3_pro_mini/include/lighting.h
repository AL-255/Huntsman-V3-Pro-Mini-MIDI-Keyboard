#ifndef HUNTSMAN_LIGHTING_H
#define HUNTSMAN_LIGHTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_config.h"

typedef struct
{
    uint8_t rgb[LIGHT_LED_COUNT][3];
    uint8_t brightness;
    bool dirty;
} lighting_state_t;

void lighting_init(lighting_state_t *state);
void lighting_set_all(lighting_state_t *state, uint8_t red, uint8_t green, uint8_t blue);
void lighting_set_key(lighting_state_t *state, size_t key, bool pressed);
void lighting_service(lighting_state_t *state);

#endif
