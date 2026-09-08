#include "lighting.h"

#include <string.h>

#include "board.h"
#include "fsl_clock.h"
#include "fsl_device_registers.h"
#include "fsl_i2c.h"

#define IOCON_I2C(func) (IOCON_PIO_FUNC(func) | IOCON_PIO_MODE(0u) | IOCON_PIO_DIGIMODE(1u))

static bool s_initialized;

static bool write_register(uint8_t address, uint8_t reg, const uint8_t *data, size_t length)
{
    i2c_master_transfer_t transfer = {
        .slaveAddress = address,
        .direction = kI2C_Write,
        .subaddress = reg,
        .subaddressSize = 1u,
        .data = (uint8_t *)data,
        .dataSize = length,
        .flags = kI2C_TransferDefaultFlag,
    };
    return I2C_MasterTransferBlocking(I2C1, &transfer) == kStatus_Success;
}

static bool write_byte(uint8_t address, uint8_t reg, uint8_t value)
{
    return write_register(address, reg, &value, 1u);
}

static bool unlock_primary(uint8_t page)
{
    return write_byte(LIGHT_PRIMARY_ADDRESS, 0xfeu, 0xc5u) &&
           write_byte(LIGHT_PRIMARY_ADDRESS, 0xfdu, page);
}

static void hardware_init(void)
{
    i2c_master_config_t config;
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM1);
    IOCON->PIO[0][13] = IOCON_I2C(1u);
    IOCON->PIO[0][14] = IOCON_I2C(1u);
    I2C_MasterGetDefaultConfig(&config);
    config.baudRate_Bps = BOARD_LIGHTING_I2C_HZ;
    config.enableTimeout = true;
    config.timeout_Ms = 10u;
    I2C_MasterInit(I2C1, &config, CLOCK_GetFlexCommClkFreq(1u));

    uint8_t zeros[LIGHT_PRIMARY_LED_COUNT * 3u] = {0};
    uint8_t secondary_current[LIGHT_SECONDARY_LED_COUNT * 3u];
    memset(secondary_current, 0x10, sizeof(secondary_current));

    (void)unlock_primary(3u);
    (void)write_byte(LIGHT_PRIMARY_ADDRESS, 0x00u, 0x05u);
    (void)write_byte(LIGHT_PRIMARY_ADDRESS, 0x01u, 0xffu);
    (void)write_byte(LIGHT_PRIMARY_ADDRESS, 0x0fu, 0x07u);
    (void)write_byte(LIGHT_PRIMARY_ADDRESS, 0x10u, 0x07u);
    (void)unlock_primary(1u);
    (void)write_register(LIGHT_PRIMARY_ADDRESS, 0x00u, zeros, sizeof(zeros));
    (void)unlock_primary(0u);
    (void)unlock_primary(1u);

    (void)write_byte(LIGHT_SECONDARY_ADDRESS, 0x2fu, 0u);
    board_delay_ms(5u);
    (void)write_byte(LIGHT_SECONDARY_ADDRESS, 0x00u, 1u);
    (void)write_register(LIGHT_SECONDARY_ADDRESS, 0x17u, secondary_current, sizeof(secondary_current));
    (void)write_byte(LIGHT_SECONDARY_ADDRESS, 0x26u, 0u);
    (void)write_byte(LIGHT_SECONDARY_ADDRESS, 0x27u, 0u);
    (void)write_register(LIGHT_SECONDARY_ADDRESS, 0x04u, zeros, LIGHT_SECONDARY_LED_COUNT * 3u);
    (void)write_byte(LIGHT_SECONDARY_ADDRESS, 0x13u, 0u);
    s_initialized = true;
}

void lighting_init(lighting_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->brightness = 0x40u;
    hardware_init();
    lighting_set_all(state, 0u, 16u, 24u);
}

void lighting_set_all(lighting_state_t *state, uint8_t red, uint8_t green, uint8_t blue)
{
    for (size_t i = 0; i < LIGHT_LED_COUNT; ++i)
    {
        state->rgb[i][0] = red;
        state->rgb[i][1] = green;
        state->rgb[i][2] = blue;
    }
    state->dirty = true;
}

void lighting_set_key(lighting_state_t *state, size_t key, bool pressed)
{
    if (key >= LIGHT_LED_COUNT)
    {
        return;
    }
    state->rgb[key][0] = pressed ? state->brightness : 0u;
    state->rgb[key][1] = pressed ? state->brightness : 16u;
    state->rgb[key][2] = pressed ? state->brightness : 24u;
    state->dirty = true;
}

void lighting_service(lighting_state_t *state)
{
    if (!s_initialized || !state->dirty)
    {
        return;
    }
    const bool primary_ok = write_register(LIGHT_PRIMARY_ADDRESS, 0x00u,
                                           &state->rgb[0][0], LIGHT_PRIMARY_LED_COUNT * 3u);
    const bool secondary_ok = write_register(LIGHT_SECONDARY_ADDRESS, 0x04u,
                                             &state->rgb[LIGHT_PRIMARY_LED_COUNT][0],
                                             LIGHT_SECONDARY_LED_COUNT * 3u) &&
                              write_byte(LIGHT_SECONDARY_ADDRESS, 0x13u, 0u);
    state->dirty = !(primary_ok && secondary_ok);
}
