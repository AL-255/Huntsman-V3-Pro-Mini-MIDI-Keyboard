#include "lighting_bus.h"
#include "board_config.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_i2c.h"

static i2c_master_handle_t s_lighting_i2c;
static volatile int s_lighting_result;

static void complete(I2C_Type *base, i2c_master_handle_t *handle, status_t status, void *user)
{
    (void)base; (void)handle; (void)user;
    s_lighting_result = status == kStatus_Success ? 1 : -1;
}

void lighting_bus_init(void)
{
    /* Production 0x2000ddc8/0x20002410: FC1, FRO12M, P0_13/P0_14=0x101.
     * SDK interrupt transactions avoid DMA's unbounded AbortTransfer path
     * and do not reinitialize the DMA controller shared with optical SPI. */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM1);
    IOCON->PIO[0][14] = 0x101u;
    IOCON->PIO[0][13] = 0x101u;
    i2c_master_config_t config;
    I2C_MasterGetDefaultConfig(&config);
    config.baudRate_Bps = BOARD_LIGHTING_I2C_HZ;
    I2C_MasterInit(I2C1, &config, CLOCK_GetFlexCommClkFreq(1u));
    I2C_MasterTransferCreateHandle(I2C1, &s_lighting_i2c, complete, NULL);
    NVIC_SetPriority(FLEXCOMM1_IRQn, 3u); /* USB and optical DMA precede LEDs. */
    const gpio_pin_config_t output = {kGPIO_DigitalOutput, 0u};
    IOCON->PIO[0][8] = IOCON->PIO[0][26] = 0x100u;
    GPIO_PinInit(GPIO, 0u, 8u, &output);
    GPIO_PinInit(GPIO, 0u, 26u, &output);
}

void lighting_bus_enable_pins(bool high)
{
    /* Both byte aliases are written by production 0x2000dfa4. */
    GPIO_PinWrite(GPIO, 0u, 8u, high);
    GPIO_PinWrite(GPIO, 0u, 26u, high);
}

bool lighting_bus_submit(uint8_t address, uint8_t reg, uint8_t *data, uint8_t size)
{
    i2c_master_transfer_t transfer = {.slaveAddress = address, .direction = kI2C_Write,
        .subaddress = reg, .subaddressSize = 1u, .data = data, .dataSize = size,
        .flags = kI2C_TransferDefaultFlag};
    s_lighting_result = 0;
    return I2C_MasterTransferNonBlocking(I2C1, &s_lighting_i2c, &transfer) == kStatus_Success;
}

int lighting_bus_result(void) { return s_lighting_result; }

void lighting_bus_quarantine(void)
{
    /* Never call SDK Abort (can spin), reinitialize the bus, or cycle GPIO.
     * Disable sources and retain the handle/payload permanently after fault. */
    I2C_DisableInterrupts(I2C1, kI2C_MasterAllInterruptEnable |
        kI2C_EventTimeoutInterruptEnable | kI2C_SclTimeoutInterruptEnable);
    DisableIRQ(FLEXCOMM1_IRQn);
}
