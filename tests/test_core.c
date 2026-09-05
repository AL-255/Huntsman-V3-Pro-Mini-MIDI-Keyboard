#include "keyboard.h"
#include "optical_scan.h"
#include "updater_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_keyboard(void)
{
    keyboard_report_t report;
    keyboard_report_clear(&report);
    assert(keyboard_report_set_usage(&report, 0x04u, true));
    assert(keyboard_report_get_usage(&report, 0x04u));
    assert(keyboard_report_set_usage(&report, 0x73u, true));
    assert(keyboard_report_get_usage(&report, 0x73u));
    assert(keyboard_report_set_usage(&report, 0xe1u, true));
    assert(report.modifiers == 0x02u);
    assert(keyboard_report_set_usage(&report, 0xe1u, false));
    assert(!keyboard_report_get_usage(&report, 0xe1u));
    assert(!keyboard_report_set_usage(&report, 0x74u, true));
    assert(keyboard_usage_for_sensor(0u) == 0x25u); /* 8, reversed ASIC order */
    assert(keyboard_usage_for_sensor(8u) == 0x29u); /* Escape */
    assert(keyboard_usage_for_sensor(43u) == 0u); /* FN after startup map patch */
    assert(keyboard_usage_for_sensor(60u) == 0x36u); /* comma */
    assert(keyboard_usage_for_sensor(61u) == 0u);
}

static void test_optical(void)
{
    uint8_t wire[OPTICAL_REPLY_SIZE(2)] = {0xc0u, 0xa0u, 0xe8u, 0x03u, 0x84u, 0x03u};
    uint16_t samples[2] = {0};
    assert(optical_scan_parse_response(wire, sizeof(wire), samples, 2u));
    assert(samples[0] == 1000u && samples[1] == 900u);
    wire[0] = 0u;
    assert(!optical_scan_parse_response(wire, sizeof(wire), samples, 2u));

    optical_scan_state_t state;
    optical_scan_init(&state, 2u);
    samples[0] = samples[1] = 1000u;
    for (unsigned i = 0; i < OPTICAL_SETTLING_FRAMES; ++i)
    {
        optical_scan_result_t result = optical_scan_process(&state, samples);
        assert(result.changed_mask_low == 0u);
    }
    samples[0] = 600u;
    optical_scan_result_t result = optical_scan_process(&state, samples);
    assert(result.changed_mask_low == 1u);
    assert(state.pressed[0] == 1u);
    samples[0] = 800u;
    result = optical_scan_process(&state, samples);
    assert(result.changed_mask_low == 1u);
    assert(state.pressed[0] == 0u);
}

static void test_updater(void)
{
    uint8_t request[UPDATER_FRAME_SIZE] = {0};
    uint8_t response[UPDATER_FRAME_SIZE] = {0};
    request[UPDATER_PAYLOAD_COUNT_OFFSET] = 22u;
    request[UPDATER_OPCODE_OFFSET] = 0x82u;
    request[UPDATER_CHECKSUM_OFFSET] = updater_frame_checksum(request);
    assert(updater_frame_valid(request, sizeof(request)));
    assert(updater_protocol_handle(request, sizeof(request), response) == kUpdaterResponseReady);
    assert(response[UPDATER_STATUS_OFFSET] == UPDATER_STATUS_SUCCESS);
    assert(response[UPDATER_PAYLOAD_COUNT_OFFSET] == 22u);
    assert(memcmp(&response[UPDATER_PAYLOAD_OFFSET], "OPENHUNTSMAN", 12u) == 0);
    assert(response[UPDATER_CHECKSUM_OFFSET] == updater_frame_checksum(response));

    memset(request, 0, sizeof(request));
    request[UPDATER_OPCODE_OFFSET] = 0x83u;
    request[UPDATER_CHECKSUM_OFFSET] = updater_frame_checksum(request);
    assert(updater_protocol_handle(request, sizeof(request), response) == kUpdaterResponseReady);
    assert(response[UPDATER_PAYLOAD_COUNT_OFFSET] == 2u);
    assert(response[UPDATER_PAYLOAD_OFFSET] == 1u);
    assert(response[UPDATER_PAYLOAD_OFFSET + 1u] == 0x34u);

    memset(request, 0, sizeof(request));
    request[UPDATER_PAYLOAD_COUNT_OFFSET] = 2u;
    request[UPDATER_OPCODE_OFFSET] = 0x04u;
    request[UPDATER_PAYLOAD_OFFSET] = 1u;
    /* Zero checksum is deliberately accepted for factory-updater compatibility. */
    assert(updater_protocol_handle(request, sizeof(request), response) == kUpdaterEnterBootloader);

    request[UPDATER_CHANNEL_OFFSET] = 1u;
    assert(updater_protocol_handle(request, sizeof(request), response) == kUpdaterNoAction);
}

int main(void)
{
    test_keyboard();
    test_optical();
    test_updater();
    puts("core tests passed");
    return 0;
}
