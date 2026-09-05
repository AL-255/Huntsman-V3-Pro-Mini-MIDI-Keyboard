#ifndef HUNTSMAN_UPDATER_PROTOCOL_H
#define HUNTSMAN_UPDATER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATER_FRAME_SIZE          90u
#define UPDATER_PAYLOAD_SIZE        80u
#define UPDATER_STATUS_OFFSET        0u
#define UPDATER_PAYLOAD_COUNT_OFFSET 5u
#define UPDATER_CHANNEL_OFFSET       6u
#define UPDATER_OPCODE_OFFSET        7u
#define UPDATER_PAYLOAD_OFFSET       8u
#define UPDATER_CHECKSUM_OFFSET     88u
#define UPDATER_EXTENSION_OFFSET    89u

#define UPDATER_STATUS_NEW          0u
#define UPDATER_STATUS_BUSY         1u
#define UPDATER_STATUS_SUCCESS      2u
#define UPDATER_STATUS_FAILURE      3u

typedef enum
{
    kUpdaterNoAction = 0,
    kUpdaterResponseReady,
    kUpdaterEnterBootloader,
} updater_action_t;

uint8_t updater_frame_checksum(const uint8_t frame[UPDATER_FRAME_SIZE]);
bool updater_frame_valid(const uint8_t *frame, size_t length);
updater_action_t updater_protocol_handle(const uint8_t *request, size_t request_length,
                                         uint8_t response[UPDATER_FRAME_SIZE]);

#endif
