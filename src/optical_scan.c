#include "optical_scan.h"

#include <string.h>

static bool sample_valid(uint16_t sample)
{
    return (sample >= 1u) && (sample <= 0x0fffu);
}

void optical_scan_init(optical_scan_state_t *state, uint8_t sensor_count)
{
    memset(state, 0, sizeof(*state));
    state->sensor_count = (sensor_count <= OPTICAL_MAX_SENSOR_COUNT) ? sensor_count : OPTICAL_MAX_SENSOR_COUNT;
}

bool optical_scan_parse_response(const uint8_t *wire, size_t wire_length,
                                 uint16_t *samples, uint8_t sensor_count)
{
    if ((wire == NULL) || (samples == NULL) || (sensor_count > OPTICAL_MAX_SENSOR_COUNT) ||
        (wire_length != OPTICAL_REPLY_SIZE(sensor_count)) || (wire[0] != 0xc0u) || (wire[1] != 0xa0u))
    {
        return false;
    }

    for (uint8_t i = 0; i < sensor_count; ++i)
    {
        samples[i] = (uint16_t)wire[2u + (2u * i)] | ((uint16_t)wire[3u + (2u * i)] << 8u);
    }
    return true;
}

uint16_t optical_scan_travel_q16(uint16_t baseline, uint16_t sample)
{
    if ((baseline < 2u) || (sample >= baseline))
    {
        return 0u;
    }
    const uint32_t travel = (uint32_t)(baseline - sample) << 16u;
    const uint32_t normalized = travel / baseline;
    return (normalized > 0xffffu) ? 0xffffu : (uint16_t)normalized;
}

optical_scan_result_t optical_scan_process(optical_scan_state_t *state,
                                           const uint16_t *samples)
{
    optical_scan_result_t result = {0};

    for (uint8_t i = 0; i < state->sensor_count; ++i)
    {
        const uint16_t sample = samples[i];
        if (!sample_valid(sample))
        {
            continue;
        }
        ++result.valid_samples;

        if ((state->settling_frames == 0u) || (state->baseline[i] == 0u) || (sample > state->baseline[i]))
        {
            state->baseline[i] = sample;
        }

        if (state->settling_frames < OPTICAL_SETTLING_FRAMES)
        {
            continue;
        }

        const uint16_t travel = optical_scan_travel_q16(state->baseline[i], sample);
        const bool next = state->pressed[i] ? (travel >= OPTICAL_RELEASE_THRESHOLD_Q16)
                                            : (travel >= OPTICAL_PRESS_THRESHOLD_Q16);
        if (next != (state->pressed[i] != 0u))
        {
            state->pressed[i] = next ? 1u : 0u;
            if (i < 64u)
            {
                result.changed_mask_low |= (UINT64_C(1) << i);
            }
            else
            {
                result.changed_mask_high |= (uint8_t)(1u << (i - 64u));
            }
        }

        /* Track slow upward drift only while released; a press must not pull the baseline down. */
        if (!state->pressed[i] && (sample > state->baseline[i]))
        {
            state->baseline[i] = sample;
        }
    }

    if ((result.valid_samples == state->sensor_count) && (state->settling_frames < OPTICAL_SETTLING_FRAMES))
    {
        ++state->settling_frames;
    }
    return result;
}
