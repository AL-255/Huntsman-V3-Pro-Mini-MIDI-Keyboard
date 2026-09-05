#ifndef DEBUG_RX_H
#define DEBUG_RX_H

#include <stddef.h>
#include <stdint.h>
#include "keyboard_console.h"

void debug_rx_receive(const uint8_t *data, size_t length);
void debug_rx_service(keyboard_console_t *console);
void debug_rx_reset(void);

#endif
