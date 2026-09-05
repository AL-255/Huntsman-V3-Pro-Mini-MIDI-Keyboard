#include "board.h"
#include "debug.h"
#include "debug_rx.h"
#include "keyboard_console.h"
#include "keyboard_live.h"
#include "usb_composite.h"

/* ASIC GPIO/SPI untouched until explicit CDC 'scan start'; host keys require
 * a separate 'keys on'. TEST commands remain isolated from physical events. */
static keyboard_console_t s_console;

int main(void)
{
    board_init();
    board_watchdog_refresh();
    debug_init();
    keyboard_console_init(&s_console, debug_write);
    keyboard_live_init();
    s_console.command = keyboard_live_command;
    usb_composite_init();
    debug_write("OpenHuntsman keyboard diagnostics; type help\r\n");
    for (;;)
    {
        usb_composite_service();
        debug_rx_service(&s_console);
        keyboard_live_service();
        debug_service();
        board_watchdog_refresh();
        __WFI();
    }
}
