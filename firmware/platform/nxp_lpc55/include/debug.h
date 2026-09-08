#ifndef HUNTSMAN_DEBUG_H
#define HUNTSMAN_DEBUG_H

#include <stddef.h>
#include <stdint.h>

void debug_init(void);
void debug_write(const char *message);
void debug_write_bytes(const uint8_t *data, size_t length);
void debug_write_hex16(const char *label, uint16_t value);
void debug_service(void);
void debug_usb_configured(void);

#endif
