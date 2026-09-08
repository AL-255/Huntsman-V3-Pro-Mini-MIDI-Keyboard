# Add a MIDI-Typist board

Start with [the architecture](ARCHITECTURE.md), then study the small
[synthetic board](../firmware/boards/synthetic/src/synthetic_board.c) and its
[main loop](../firmware/boards/synthetic/src/main.c). They build without NXP
headers, stock firmware, a keyboard or a USB library.

## 1. Create a board directory and build target

Add `firmware/boards/<name>/board.cmake` and your board sources. Select it with
`-DMT_BOARD=<name>`; use an appropriate CMake toolchain for a cross build.
The root includes `firmware/app/CMakeLists.txt` and your board manifest.
Do not add device checks to `firmware/app` or copy its algorithms into the port.

Compile `MT_APP_SOURCES` into an object target with only
`firmware/app/include` visible. Add SDK include paths to hardware targets,
not to the application target. Apply the same ABI options to both: CPU,
instruction set, float ABI, alignment restrictions and structure layout.
The provided board manifests are complete examples of target wiring.

Select consistent capacities for every object that includes public headers:

| Definition | Default | Huntsman |
| --- | ---: | ---: |
| `MT_KEY_CAPACITY` | 128 | 65 |
| `MT_LIGHT_FRAME_BYTES` | 3 × capacity | 204 |
| `MT_HID_USAGE_MAX` | 0xDF | 0x73 |

Counts must be nonzero and no greater than capacity; capacity must be below
255. Modifier usages E0…E7 are handled separately. Match your HID descriptor
to `KEYBOARD_NKRO_REPORT_BYTES`; do not reuse the Huntsman descriptor when
choosing the wider default report. Arrays are fixed-capacity, not allocated
per interrupt. Inspect the linker map and stack margins for your own MCU.

## 2. Describe keys independently of scan order

Implement the functions declared in
[keyboard_layout.h](../firmware/app/include/keyboard_layout.h):

- `keyboard_layout`: immutable description or NULL for an unknown layout ID.
- `keyboard_key_for_sensor`: scan index to opaque board-local key ID.
- `keyboard_action`: base/Fn actions for each valid key ID.
- `keyboard_lower_group`: membership in the optional lower MIDI group.
- Editor digit/step/exclusion, actuation-pair and travel-level queries.

Key IDs 0 and 255 are reserved. Use unique nonzero IDs, independently of
USB HID usages. A plain keyboard action uses type 2, modifier bits in
`arg0`, and HID usage in `arg1`. Fn is identified by the layout's
`fn` field, not by its ID value or scan position. Editor actions use type
0x11 with arg0 0x70 (trigger) or 0x71 (rapid), as the example demonstrates.
Other action types are consumed/unmapped, not guessed as keyboard reports.

The application maps MIDI notes and control roles from base HID semantics.
Its musical selector tables are independent of custom note mappings.
Choose `compact_navigation=true` only if the board should replace Right
Alt/Menu/Right Ctrl/Right Shift with arrows in keyboard mode; a full-size
board can leave it false. The Huntsman enables it.

Describe the board's supported editor keys even if their physical arrangement
differs. A keyboard missing a menu letter cannot show that letter or offer
that physical selector; do not silently invent a hardware key. Board-local
action mappings can assign suitable physical controls.

## 3. Acquire real analog samples

Implement clocks, watchdog/power sequencing, ADC/ASIC scanning and readiness
using the MCU vendor's supported peripheral libraries. Supply one complete
frame per real acquisition, in the described sensor order.

Canonical values are 1…4096 and decrease on press. For an ascending 16-bit
ADC, `keyboard_sample_normalize(value,0,65535,&sample)` maps its full scale
to 4096…1. This conversion is not per-key travel calibration. Choose electrical
full-scale endpoints and retain real travel variation for calibration to learn.
Equal conversion endpoints are rejected. Bus/ADC errors must make the frame
invalid rather than becoming a valid zero-pressure sample.

Set `sample_hz` to the actual intended frame rate. The five post-trigger
samples span four intervals, so a different frame rate changes the velocity
multiplier. Nominal configuration is not a measurement of hardware timing:
measure cadence, dropped frames and worst-case service time under polyphony.

## 4. Connect the common lifecycle

Allocate `keyboard_raw_t`, `keyboard_midi_t`, `keyboard_menu_t`,
`keyboard_calibration_t` and `keyboard_app_t` in suitable RAM. Call
`keyboard_app_init` once, then:

1. Service your USB/peripheral stack and copy/publish completed acquisitions.
2. For each complete acquisition, call `keyboard_app_frame` with canonical
   samples, layout/count, mutable lower/upper bounds, validity and milliseconds.
3. Call `keyboard_app_lights`, then submit the board framebuffer safely.
4. Call `keyboard_app_service` frequently, including between acquisitions,
   with hardware/USB health and keyboard/MIDI send callbacks.
5. Refresh the actual board watchdog and wait using your platform's mechanism.

Callbacks must not reenter application code. A USB reset/discontinuity calls
`keyboard_app_invalidate` in the owner context, even if USB reconfigures
before the next scan. Do not call it directly from an interrupt. No data for
100 ms disarms output; a known transport fault must be reported immediately.
If the layout changes, staged calibration is discarded before reading the
new frame, and calibration storage is loaded for the new layout.

The send callbacks copy or take ownership before returning true. Return false
while busy. This preserves the ordered Note On/Off/sustain queue and immutable
HID submissions without a port-specific copy of the performance engine.

## 5. Add lighting, storage and host integration

`keyboard_light_set` writes RGB intensities into the supplied byte framebuffer
for one sensor. Zero means off, 255 maximum. An unlit key may be ignored by
the setter; its scan/key behavior still works. If LEDs require gamma conversion,
bit packing or command headers, encode those at hardware submission, after the
application's linear intensity/brightness processing. Preserve a transfer
snapshot until the peripheral has finished using it.

Provide calibration load/save/profile-clear callbacks as appropriate. The
callbacks own physical pages, checksums, rollback, identity and power-failure
handling. They must not modify unrelated bootloader, serial, security or
factory data. No callback means unavailable storage; calibration must report
failure instead of claiming a persistent save. The simulator saves only in
its process RAM. Huntsman's writer and HKC1 journal are examples for that board,
not a universal flash layout.

USB normally exposes NKRO HID, USB-MIDI and CDC through the platform's stack.
Feed newline-stripped configuration commands to `keyboard_app_command`;
it implements get/set/all/enable/MIDI/calibrate/cancel validation and ACK
semantics. Board diagnostics, telemetry serialization and the MCU's firmware
update path remain in the port. Never copy the Huntsman reset cookie or flash
addresses to an unrelated bootloader.

The existing GUI/scan tools understand Huntsman wire formats and physical
geometry. They are not generic MCU discovery or flashing tools. Reusing those
formats requires matching their layout/size contracts; a different host
presentation can call the same common configuration command engine.

## 6. Prove the port

Build/test the shared code without another board's include directories or
source files. Adapt the synthetic test to your descriptor and test:

- Normal and Fn HID output, duplicates, release order and USB backpressure.
- Every sensor independently, including indices above 64 where present.
- Velocity cadence, overlap/pop filtering, Note Off and sustain ordering.
- Root/scale and row filters, controls and LED positions.
- Calibration parallel holds, cancel, save failures, neutral arming and reset.
- Invalid samples, short frames, layout changes, stale input and USB reset.
- All protected flash boundaries and the computer-initiated updater path.

Then validate on hardware with a recoverable application-only update and
readback. A simulator or register model does not prove pin routing, electrical
power behavior, optical timing or real USB signal integrity.
