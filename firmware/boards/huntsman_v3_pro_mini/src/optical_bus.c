#include "optical_bus.h"
#include "board_config.h"
#include "fsl_clock.h"
#include "fsl_ctimer.h"
#include "fsl_dma.h"
#include "fsl_gpio.h"
#include "fsl_spi_dma.h"

static dma_handle_t s_tx_dma, s_rx_dma;
static spi_dma_handle_t s_spi_dma;
static volatile int s_result;
static volatile uint32_t s_ticks;

static void output(unsigned pin, unsigned value)
{
    const gpio_pin_config_t config = {kGPIO_DigitalOutput, value};
    IOCON->PIO[0][pin] = 0x100u;
    GPIO_PinInit(GPIO, 0u, pin, &config);
}

void optical_bus_begin(void)
{
    /* Exact pin states/order from 0x2000ca68, 0x200174dc, 0x20017528.
     * Their electrical roles are not inferred from the GPIO numbers. */
    GPIO_PortInit(GPIO, 0u);
    GPIO_PortInit(GPIO, 1u);
    output(8u, 0u);
    output(26u, 0u);
    output(29u, 0u);
    output(30u, 1u);
    GPIO_PinWrite(GPIO, 0u, 29u, 0u);
    GPIO_PinWrite(GPIO, 0u, 30u, 1u);
}

void optical_bus_route(void)
{
    /* Called after 10 ms; wait another 150 ms before SPI initialization. */
    IOCON->PIO[0][19] = 0x120u;
    output(6u, 0u);
    GPIO_PinWrite(GPIO, 0u, 30u, 0u);
    GPIO_PinWrite(GPIO, 0u, 29u, 1u);
}

static void completed(SPI_Type *base, spi_dma_handle_t *handle, status_t status, void *user)
{
    (void)base; (void)handle; (void)user;
    s_result = status == kStatus_Success ? 1 : -1;
}

static void tick(uint32_t flags) { (void)flags; ++s_ticks; }
static ctimer_callback_t s_tick_callback = tick;

bool optical_bus_enable(void)
{
    /* Production 0x2000dbac / 0x20002424: SPI3, mode 1, 8 MHz, SSEL0. */
    CLOCK_SetClkDiv(kCLOCK_DivFrohfClk, 1u, false);
    CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCOMM3);
    IOCON->PIO[0][2] = 0x151u;
    IOCON->PIO[0][3] = 0x141u;
    IOCON->PIO[0][6] = 0x141u;
    IOCON->PIO[0][4] = 0x148u;
    spi_master_config_t config;
    SPI_MasterGetDefaultConfig(&config);
    config.baudRate_Bps = BOARD_OPTICAL_SPI_HZ;
    config.polarity = kSPI_ClockPolarityActiveHigh;
    config.phase = kSPI_ClockPhaseSecondEdge;
    config.dataWidth = kSPI_Data8Bits;
    config.sselNum = kSPI_Ssel0;
    if (SPI_MasterInit(SPI3, &config, CLOCK_GetFlexCommClkFreq(3u)) != kStatus_Success) return false;
    DMA_Init(DMA0);
    /* LPC5528 uses fixed DMA request mapping; production 0x2000156c
     * enables channels, not the LPC55S69-only inputmux request gates. */
    DMA_EnableChannel(DMA0, 9u);
    DMA_EnableChannel(DMA0, 8u);
    DMA_SetChannelPriority(DMA0, 9u, (dma_priority_t)3u);
    DMA_SetChannelPriority(DMA0, 8u, (dma_priority_t)2u);
    DMA_CreateHandle(&s_tx_dma, DMA0, 9u);
    DMA_CreateHandle(&s_rx_dma, DMA0, 8u);
    SPI_MasterTransferCreateHandleDMA(SPI3, &s_spi_dma, completed, NULL, &s_tx_dma, &s_rx_dma);
    NVIC_SetPriority(DMA0_IRQn, 2u);

    ctimer_config_t timer;
    const ctimer_match_config_t match = {
        .matchValue = 11999u, .enableCounterReset = true,
        .outControl = kCTIMER_Output_NoAction, .enableInterrupt = true,
    };
    CLOCK_AttachClk(kFRO_HF_to_CTIMER2);
    CTIMER_GetDefaultConfig(&timer);
    CTIMER_Init(CTIMER2, &timer);
    CTIMER_SetupMatch(CTIMER2, kCTIMER_Match_1, &match);
    CTIMER_RegisterCallBack(CTIMER2, &s_tick_callback, kCTIMER_SingleCallback);
    NVIC_SetPriority(CTIMER2_IRQn, 3u);
    CTIMER_StartTimer(CTIMER2);
    return true;
}

bool optical_bus_ready(void) { return GPIO_PinRead(GPIO, 0u, 19u) == 0u; }
uint32_t optical_bus_ticks(void) { return s_ticks; }
int optical_bus_result(void) { return s_result; }

bool optical_bus_submit(uint8_t *tx, uint8_t *rx, uint16_t length)
{
    spi_transfer_t transfer = {.txData = tx, .rxData = rx, .dataSize = length,
                               .configFlags = kSPI_FrameAssert};
    s_result = 0;
    return SPI_MasterTransferDMA(SPI3, &s_spi_dma, &transfer) == kStatus_Success;
}

void optical_bus_quarantine(void)
{
    /* SDK DMA_AbortTransfer spins on BUSY. Disable requests/IRQs instead;
     * retain descriptors and buffers forever so a late bus access is safe.
     * No DMA reset, SPI reinit, GPIO cycle, or automatic retry follows. */
    CTIMER_StopTimer(CTIMER2);
    SPI_EnableTxDMA(SPI3, false);
    SPI_EnableRxDMA(SPI3, false);
    DMA_DisableChannel(DMA0, 9u);
    DMA_DisableChannel(DMA0, 8u);
    DMA_DisableChannelInterrupts(DMA0, 9u);
    DMA_DisableChannelInterrupts(DMA0, 8u);
}
