#ifndef HUNTSMAN_BOARD_CONFIG_H
#define HUNTSMAN_BOARD_CONFIG_H

#include <stdint.h>

#define BOARD_CORE_CLOCK_HZ       96000000u
#define BOARD_XTAL_CLOCK_HZ       16000000u
#define BOARD_OPTICAL_SPI_HZ       8000000u
#define BOARD_LIGHTING_I2C_HZ       400000u

#define OPTICAL_SENSOR_COUNT             61u
#define OPTICAL_MAX_SENSOR_COUNT         65u
#define OPTICAL_REPLY_SIZE(count) ((uint16_t)(2u + (2u * (count))))

#define LIGHT_PRIMARY_ADDRESS          0x50u
#define LIGHT_SECONDARY_ADDRESS        0x6cu
#define LIGHT_PRIMARY_LED_COUNT          64u
#define LIGHT_SECONDARY_LED_COUNT         4u
#define LIGHT_LED_COUNT                  68u

#define UPDATER_RESET_COOKIE_ADDRESS 0x2002fffcu
#define UPDATER_RESET_COOKIE         0xaaaaaaaau

#endif
