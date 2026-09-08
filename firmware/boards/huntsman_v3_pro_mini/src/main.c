#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "board_config.h"
#include "debug.h"
#include "keyboard.h"
#include "huntsman_layout.h"
#include "lighting.h"
#include "optical_hw.h"
#include "optical_scan.h"
#include "usb_composite.h"

int main(void)
{
    optical_scan_state_t scan;
    lighting_state_t lighting;
    keyboard_report_t keyboard;
    uint16_t samples[OPTICAL_MAX_SENSOR_COUNT];
    uint32_t last_light_update = 0u;

    board_init();
    board_watchdog_refresh();
    debug_init();
    keyboard_report_clear(&keyboard);
    optical_scan_init(&scan, OPTICAL_SENSOR_COUNT);
    usb_composite_init();
    debug_write("OpenHuntsman USB initialized\r\n");

    /* Service EP0 before any optional off-chip controller can delay startup. */
    const uint32_t usb_settle_start = board_millis();
    while ((uint32_t)(board_millis() - usb_settle_start) < 500u)
    {
        usb_composite_service();
        board_watchdog_refresh();
    }

    /* USB continues to be serviced while the board peripherals settle. */
    optical_hw_init();
    board_watchdog_refresh();
    lighting_init(&lighting);
    debug_write("OpenHuntsman application ready\r\n");

    (void)optical_hw_set_mode(1u);

    for (;;)
    {
        usb_composite_service();
        debug_service();
        board_watchdog_refresh();

        if (optical_hw_ready() && optical_hw_read(samples, scan.sensor_count))
        {
            const optical_scan_result_t result = optical_scan_process(&scan, samples);
            if ((result.changed_mask_low != 0u) || (result.changed_mask_high != 0u))
            {
                for (uint8_t sensor = 0u; sensor < scan.sensor_count; ++sensor)
                {
                    const bool changed = (sensor < 64u)
                        ? ((result.changed_mask_low & (UINT64_C(1) << sensor)) != 0u)
                        : ((result.changed_mask_high & (uint8_t)(1u << (sensor - 64u))) != 0u);
                    if (!changed)
                    {
                        continue;
                    }
                    const uint8_t usage = keyboard_usage_for_sensor(sensor);
                    if (usage != 0u)
                    {
                        (void)keyboard_report_set_usage(&keyboard, usage, scan.pressed[sensor] != 0u);
                    }
                    lighting_set_key(&lighting, sensor, scan.pressed[sensor] != 0u);
                    debug_write_hex16(scan.pressed[sensor] ? "down " : "up   ", usage);
                }
                (void)usb_keyboard_send(&keyboard);
            }
        }

        const uint32_t now = board_millis();
        if ((uint32_t)(now - last_light_update) >= 10u)
        {
            last_light_update = now;
            lighting_service(&lighting);
        }
        __WFI();
    }
}
