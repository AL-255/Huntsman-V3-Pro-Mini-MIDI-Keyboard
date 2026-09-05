#include "board.h"
#include "debug.h"
#include "keyboard.h"
#include "usb_composite.h"

/* USB bring-up only. No optical/LED initialization, SPI, or I2C transactions.
 * Keep all composite interfaces and the host updater entry path available.
 * Only neutral keyboard reports are generated; no synthetic key presses. */
int main(void)
{
    keyboard_report_t neutral;
    board_init();
    board_watchdog_refresh();
    debug_init();
    keyboard_report_clear(&neutral);
    usb_composite_init();
    debug_write("OpenHuntsman USB-only bring-up\r\n");
    uint32_t last_heartbeat = board_millis();

    for (;;)
    {
        usb_composite_service();
        debug_service();
        board_watchdog_refresh();
        const uint32_t now = board_millis();
        if ((uint32_t)(now - last_heartbeat) >= 1000u)
        {
            last_heartbeat = now;
            debug_write("USB service alive\r\n");
            (void)usb_keyboard_send(&neutral);
        }
        __WFI();
    }
}
