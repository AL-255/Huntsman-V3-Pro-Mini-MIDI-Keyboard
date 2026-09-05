#ifndef HUNTSMAN_BOARD_H
#define HUNTSMAN_BOARD_H

#include <stdint.h>

void board_init(void);
void board_usb_clock_init(void);
void board_usb_isr_enable(void);
void board_watchdog_refresh(void);
uint32_t board_millis(void);
void board_delay_ms(uint32_t milliseconds);
void board_enter_bootloader(void) __attribute__((noreturn));

#endif
