#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H
#include "keyboard_calibration.h"
#define CAL_PAGE_SIZE 512u
#define CAL_SLOT_A 0x7d400u
#define CAL_SLOT_B 0x7d600u
typedef uint32_t (*cal_read_fn)(unsigned slot, uint8_t *page);
typedef uint32_t (*cal_write_fn)(unsigned slot, const uint8_t *page);
typedef struct { uint32_t generation, error; uint8_t slot; bool saved; } calibration_store_t;
uint32_t calibration_crc32(const uint8_t *p, unsigned n);
void calibration_record(uint8_t *page, uint8_t profile, uint8_t count, uint32_t gen, const uint16_t *lo, const uint16_t *hi);
bool calibration_record_valid(const uint8_t *page);
void calibration_store_load(calibration_store_t *s, uint8_t profile, uint8_t count, uint16_t *lo, uint16_t *hi, cal_read_fn read);
bool calibration_store_save(calibration_store_t *s, const keyboard_calibration_t *cal, cal_read_fn read, cal_write_fn write);
#endif
