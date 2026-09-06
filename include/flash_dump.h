#ifndef FLASH_DUMP_H
#define FLASH_DUMP_H
#include <stdint.h>
#define FLASH_DUMP_SIZE 128u
#define FLASH_DUMP_CHUNK 64u
/* Application/bootloader and stock user-storage only; never PFR/ROM/MMIO.
 * Further bounded by the SDK-reported physical main-flash geometry. */
#define FLASH_DUMP_LIMIT 0x7f400u
void flash_dump_record(uint32_t id, uint32_t address, uint8_t out[FLASH_DUMP_SIZE]);
uint32_t flash_calibration_read(unsigned slot, uint8_t *page);
uint32_t flash_calibration_write(unsigned slot, const uint8_t *page);
#endif
