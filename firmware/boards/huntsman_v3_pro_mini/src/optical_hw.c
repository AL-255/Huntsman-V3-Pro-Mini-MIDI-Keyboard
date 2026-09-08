#include "optical_hw.h"

#include <string.h>

#include "board.h"
#include "board_config.h"
#include "fsl_clock.h"
#include "fsl_device_registers.h"
#include "fsl_gpio.h"
#include "fsl_spi.h"
#include "optical_scan.h"

#define IOCON_DIGITAL(func, mode) \
    (IOCON_PIO_FUNC(func) | IOCON_PIO_MODE(mode) | IOCON_PIO_SLEW(1u) | \
     IOCON_PIO_DIGIMODE(1u) | IOCON_PIO_FILTEROFF(1u))

static uint8_t s_tx[OPTICAL_REPLY_SIZE(OPTICAL_MAX_SENSOR_COUNT)];
static uint8_t s_rx[OPTICAL_REPLY_SIZE(OPTICAL_MAX_SENSOR_COUNT)];

static void route_enable(bool enable)
{
    if (enable)
    {
        GPIO_PortClear(GPIO, 0u, 1u << 29u);
        GPIO_PortSet(GPIO, 0u, 1u << 30u);
    }
    else
    {
        GPIO_PortClear(GPIO, 0u, 1u << 30u);
        GPIO_PortSet(GPIO, 0u, 1u << 29u);
    }
}

void optical_hw_init(void)
{
    const gpio_pin_config_t output_low = {kGPIO_DigitalOutput, 0u};
    const gpio_pin_config_t output_high = {kGPIO_DigitalOutput, 1u};
    const gpio_pin_config_t input = {kGPIO_DigitalInput, 0u};

    CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCOMM3);
    CLOCK_SetClkDiv(kCLOCK_DivFrohfClk, 1u, false);

    IOCON->PIO[0][2] = IOCON_DIGITAL(1u, 1u);  /* FC3 SCK */
    IOCON->PIO[0][3] = IOCON_DIGITAL(1u, 0u);  /* FC3 MOSI */
    IOCON->PIO[0][4] = IOCON_DIGITAL(8u, 0u);  /* FC3 MISO */
    IOCON->PIO[0][6] = IOCON_DIGITAL(1u, 0u);  /* FC3 SSEL0 */
    IOCON->PIO[0][19] = IOCON_DIGITAL(0u, 2u); /* active-low ready */
    IOCON->PIO[0][8] = IOCON_DIGITAL(0u, 0u);
    IOCON->PIO[0][26] = IOCON_DIGITAL(0u, 0u);
    IOCON->PIO[0][29] = IOCON_DIGITAL(0u, 0u);
    IOCON->PIO[0][30] = IOCON_DIGITAL(0u, 0u);

    GPIO_PinInit(GPIO, 0u, 8u, &output_low);
    GPIO_PinInit(GPIO, 0u, 19u, &input);
    GPIO_PinInit(GPIO, 0u, 26u, &output_low);
    GPIO_PinInit(GPIO, 0u, 29u, &output_high);
    GPIO_PinInit(GPIO, 0u, 30u, &output_low);

    spi_master_config_t config;
    SPI_MasterGetDefaultConfig(&config);
    config.baudRate_Bps = BOARD_OPTICAL_SPI_HZ;
    config.polarity = kSPI_ClockPolarityActiveHigh;
    config.phase = kSPI_ClockPhaseSecondEdge;
    config.direction = kSPI_MsbFirst;
    config.dataWidth = kSPI_Data8Bits;
    config.sselNum = kSPI_Ssel0;
    config.sselPol = kSPI_SpolActiveAllLow;
    (void)SPI_MasterInit(SPI3, &config, CLOCK_GetFlexCommClkFreq(3u));

    route_enable(true);
    board_delay_ms(10u);
    route_enable(false);
    board_delay_ms(150u);
    route_enable(true);
}

bool optical_hw_ready(void)
{
    return GPIO_PinRead(GPIO, 0u, 19u) == 0u;
}

static bool transfer(size_t length)
{
    spi_transfer_t transfer = {
        .txData = s_tx,
        .rxData = s_rx,
        .dataSize = length,
        .configFlags = kSPI_FrameAssert,
    };
    return SPI_MasterTransferBlocking(SPI3, &transfer) == kStatus_Success;
}

bool optical_hw_set_mode(uint8_t mode)
{
    memset(s_tx, 0, sizeof(s_tx));
    s_tx[0] = 0xb6u;
    s_tx[1] = mode;
    return transfer(2u);
}

bool optical_hw_read(uint16_t *samples, uint8_t sensor_count)
{
    const size_t length = OPTICAL_REPLY_SIZE(sensor_count);
    if ((samples == NULL) || (sensor_count > OPTICAL_MAX_SENSOR_COUNT))
    {
        return false;
    }
    memset(s_tx, 0, length);
    memset(s_rx, 0, length);
    s_tx[0] = 0xa0u;
    s_tx[1] = 0u;
    if (!transfer(length))
    {
        return false;
    }

    /* Some ASIC revisions echo the request before returning c0/a0 one frame later. */
    if (!optical_scan_parse_response(s_rx, length, samples, sensor_count))
    {
        return false;
    }
    return true;
}
