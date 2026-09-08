#include "usb_errata.h"

#include <stdbool.h>
#include <stdint.h>

#include "fsl_clock.h"
#include "fsl_ctimer.h"
#include "fsl_device_registers.h"

/*
 * HS PHY chirp recovery as used by the production application.
 * CTIMER3 runs at 96 MHz and supplies the 1 ms waits from IRQ priority zero,
 * which is high enough to preempt the USB1 bus-reset callback.
 */
#define USB_FORCE_FS_MASK       (1u << 21u)
#define USB_FRAME_NUMBER_MASK   0x7ffu
#define USB_W1C_AND_TEST_MASK   0xef000000u
#define USB_RECONNECT_MASK      0xf0feffffu
#define USB_RECONNECT_CHANGE    0x04010000u
#define USB_TIMER_MATCH_VALUE   95999u

static volatile uint32_t s_timer_ticks;
static bool s_connected_to_fs_host;
static bool s_connected_to_hs_host;

static void timer_stop(void)
{
    CTIMER_StopTimer(CTIMER3);
    CTIMER_ClearStatusFlags(CTIMER3, kCTIMER_Match0Flag);
}

static void timer_callback(uint32_t flags)
{
    (void)flags;
    if ((s_timer_ticks == 0u) || (--s_timer_ticks == 0u))
    {
        timer_stop();
    }
}

static ctimer_callback_t s_timer_callback = timer_callback;

static void timer_wait(uint32_t ticks)
{
    s_timer_ticks = ticks;
    CTIMER_StartTimer(CTIMER3);
    while (s_timer_ticks != 0u)
    {
        __NOP();
    }
}

void usb_errata_init(void)
{
    ctimer_config_t timer_config;
    const ctimer_match_config_t match_config = {
        .matchValue = USB_TIMER_MATCH_VALUE,
        .enableCounterReset = true,
        .enableCounterStop = false,
        .outControl = kCTIMER_Output_NoAction,
        .outPinInitState = false,
        .enableInterrupt = true,
    };

    CLOCK_AttachClk(kFRO_HF_to_CTIMER3);
    CTIMER_GetDefaultConfig(&timer_config);
    CTIMER_Init(CTIMER3, &timer_config);
    CTIMER_SetupMatch(CTIMER3, kCTIMER_Match_0, &match_config);
    CTIMER_RegisterCallBack(CTIMER3, &s_timer_callback, kCTIMER_SingleCallback);
    NVIC_SetPriority(CTIMER3_IRQn, 0u);
    timer_stop();
}

void usb_errata_bus_reset(void)
{
    /* Actual instruction 0x20005ca2 reads INFO before the latch/speed tests.
     * The decompiler moves this read and merges the two disconnect writes;
     * neither transformation preserves the observable MMIO sequence. */
    const uint32_t start_frame = USBHSD->INFO & USB_FRAME_NUMBER_MASK;
    if (s_connected_to_hs_host || s_connected_to_fs_host)
    {
        return;
    }
    if ((USBHSD->DEVCMDSTAT & USBHSD_DEVCMDSTAT_Speed_MASK) !=
        USBHSD_DEVCMDSTAT_Speed(1u))
    {
        return;
    }

    bool full_speed = false;

    USBHSD->DEVCMDSTAT =
        (USBHSD->DEVCMDSTAT & ~USB_W1C_AND_TEST_MASK) |
        USBHSD_DEVCMDSTAT_PHY_TEST_MODE(5u);
    timer_wait(100u);
    if ((USBHSD->INFO & USB_FRAME_NUMBER_MASK) == start_frame)
    {
        timer_wait(1u);
        full_speed = (USBHSD->INFO & USB_FRAME_NUMBER_MASK) == start_frame;
    }

    if (!full_speed)
    {
        s_connected_to_hs_host = true;
    }

    USBHSD->DEVCMDSTAT &= ~USB_W1C_AND_TEST_MASK;
    USBHSD->DEVCMDSTAT &= USB_RECONNECT_MASK;
    timer_wait(1u);
    /* Keep the individual volatile stores from 0x20005d46..0x20005d7a.
     * In particular, acknowledge change bit 24 before reconnecting. The
     * decompiled C incorrectly folds these observable writes together. */
    if (full_speed)
        USBHSD->DEVCMDSTAT |= USB_FORCE_FS_MASK;
    USBHSD->DEVCMDSTAT = (USBHSD->DEVCMDSTAT & ~0x0f000000u) | 0x01000000u;
    USBHSD->DEVCMDSTAT =
        (USBHSD->DEVCMDSTAT & USB_RECONNECT_MASK) |
        USB_RECONNECT_CHANGE;
    if (full_speed)
    {
        s_connected_to_fs_host = true;
    }
}
