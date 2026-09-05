#ifndef SCAN_STREAM_H
#define SCAN_STREAM_H
#include <stdbool.h>
#include <stdint.h>
#define SCAN_STREAM_RECORD_SIZE 160u
#define SCAN_STREAM_KEY_SIZE 20u
#define SCAN_STREAM_GUI_SIZE 1152u
void scan_stream_gui(void);
bool scan_stream_gui_enabled(void);
void scan_stream_gui_push(const uint8_t report[SCAN_STREAM_GUI_SIZE]);
void scan_stream_last_key(uint16_t threshold, uint32_t session);
void scan_stream_whole(void);
void scan_stream_init(void);
void scan_stream_start(void);
void scan_stream_stop(void);
bool scan_stream_active(void);
bool scan_stream_enabled(void);
void scan_stream_push(const uint16_t *samples, uint8_t count, uint8_t profile, uint32_t tick);
bool scan_stream_service(void); /* true while owning CDC IN */
void scan_stream_complete(void);
void scan_stream_usb_reset(void);
uint32_t scan_stream_dropped(void);
#endif
