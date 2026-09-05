#include "board.h"

#include "board_config.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_device_registers.h"
#include "fsl_power.h"
#include "fsl_reset.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_config.h"
#include "usb_phy.h"

static volatile uint32_t s_milliseconds;

void SysTick_Handler(void)
{
    ++s_milliseconds;
}

static void clocks_init(void)
{
    POWER_DisablePD(kPDRUNCFG_PD_FRO192M);
    CLOCK_SetupFROClocking(12000000u);
    CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);
    CLOCK_SetupFROClocking(BOARD_CORE_CLOCK_HZ);
    POWER_SetVoltageForFreq(BOARD_CORE_CLOCK_HZ);
    CLOCK_SetFLASHAccessCyclesForFreq(BOARD_CORE_CLOCK_HZ);
    CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1u, false);
    CLOCK_AttachClk(kFRO_HF_to_MAIN_CLK);
    SystemCoreClock = BOARD_CORE_CLOCK_HZ;
}

void board_init(void)
{
    clocks_init();
    CLOCK_EnableClock(kCLOCK_Iocon);
    CLOCK_EnableClock(kCLOCK_Gpio0);

    /* The production keyboard supplies a 16 MHz crystal/clock to the USB PLL. */
    POWER_DisablePD(kPDRUNCFG_PD_XTAL32M);
    POWER_DisablePD(kPDRUNCFG_PD_LDOXO32M);
    CLOCK_SetupExtClocking(BOARD_XTAL_CLOCK_HZ);
    SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK;
    ANACTRL->XO32M_CTRL |= ANACTRL_XO32M_CTRL_ENABLE_SYSTEM_CLK_OUT_MASK;

    (void)SysTick_Config(BOARD_CORE_CLOCK_HZ / 1000u);
}

void board_usb_clock_init(void)
{
    DisableIRQ(USB0_IRQn);
    DisableIRQ(USB0_NEEDCLK_IRQn);
    DisableIRQ(USB1_IRQn);
    DisableIRQ(USB1_NEEDCLK_IRQn);
    NVIC_ClearPendingIRQ(USB0_IRQn);
    NVIC_ClearPendingIRQ(USB0_NEEDCLK_IRQn);
    NVIC_ClearPendingIRQ(USB1_IRQn);
    NVIC_ClearPendingIRQ(USB1_NEEDCLK_IRQn);

    POWER_DisablePD(kPDRUNCFG_PD_USB0_PHY);
    POWER_DisablePD(kPDRUNCFG_PD_USB1_PHY);
    RESET_PeripheralReset(kUSB0D_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB0HSL_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB0HMR_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1H_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1D_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1_RST_SHIFT_RSTn);
    RESET_PeripheralReset(kUSB1RAM_RST_SHIFT_RSTn);

    CLOCK_EnableClock(kCLOCK_Usbh1);
    *((volatile uint32_t *)(USBHSH_BASE + 0x50u)) = USBHSH_PORTMODE_SW_PDCOM_MASK;
    *((volatile uint32_t *)(USBHSH_BASE + 0x50u)) |= USBHSH_PORTMODE_DEV_ENABLE_MASK;
    CLOCK_DisableClock(kCLOCK_Usbh1);

    (void)CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_UsbPhySrcExt, BOARD_XTAL_CLOCK_HZ);
    (void)CLOCK_EnableUsbhs0DeviceClock(kCLOCK_UsbSrcUnused, 0u);
    (void)USB_EhciPhyInit((uint8_t)kUSB_ControllerLpcIp3511Hs0,
                          BOARD_XTAL_CLOCK_HZ, NULL);

    for (uint32_t i = 0u; i < FSL_FEATURE_USBHSD_USB_RAM; ++i)
    {
        ((volatile uint8_t *)FSL_FEATURE_USBHSD_USB_RAM_BASE_ADDRESS)[i] = 0u;
    }
}

void board_usb_isr_enable(void)
{
    const uint8_t irqs[] = USBHSD_IRQS;
    const IRQn_Type irq = (IRQn_Type)irqs[kUSB_ControllerLpcIp3511Hs0 - kUSB_ControllerLpcIp3511Hs0];
    NVIC_SetPriority(irq, 1u);
    EnableIRQ(irq);
}

void board_watchdog_refresh(void)
{
    if ((WWDT->MOD & WWDT_MOD_WDEN_MASK) != 0u)
    {
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        WWDT->FEED = 0xaau;
        WWDT->FEED = 0x55u;
        __set_PRIMASK(primask);
    }
}

uint32_t board_millis(void)
{
    return s_milliseconds;
}

void board_delay_ms(uint32_t milliseconds)
{
    const uint32_t start = board_millis();
    while ((uint32_t)(board_millis() - start) < milliseconds)
    {
        __WFI();
    }
}

void board_enter_bootloader(void)
{
    __disable_irq();
    *(volatile uint32_t *)UPDATER_RESET_COOKIE_ADDRESS = UPDATER_RESET_COOKIE;
    __DSB();
    __ISB();
    NVIC_SystemReset();
    for (;;)
    {
    }
}
