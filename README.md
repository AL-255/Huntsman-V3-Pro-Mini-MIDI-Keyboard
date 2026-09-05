# Huntsman V3 Pro Mini MIDI firmware

An independent LPC5528 application for the Razer Huntsman V3 Pro Mini. It is
designed for the keyboard's existing bootloader and the 128 KiB application
image linked at `0x20000000`; the bootloader is neither included nor modified.

Status: development checkpoint, **not ready to flash**. Earlier images failed
USB enumeration. An instruction-level USB alignment defect has been reproduced
offline and corrected, but optical and lighting startup still have known gaps.
See [the bring-up audit](docs/BRINGUP_AUDIT.md) for evidence, corrections to
earlier claims, test coverage, and the remaining flashing blockers.

The current implementation contains:

- optical-key scanning over Flexcomm 3 SPI at 8 MHz, including ASIC ready and
  route/control GPIOs, response validation, 128-frame baseline settling, and
  actuation/release hysteresis;
- RGB output over Flexcomm 1 I2C at 400 kHz for the 64-LED primary controller
  (`0x50`) and four-LED secondary controller (`0x6c`);
- a six-interface USB 2.0 composite device using NXP's IP3511 HS device stack;
- an NKRO HID keyboard, USB-MIDI 1.0 streaming endpoints, firmware-update HID
  feature transport at interface 3, and CDC ACM debug output;
- host-side tests for scan processing, NKRO packing, and updater framing,
  post-link validation of the image and USB descriptors, and offline execution
  tests of the compiled ARM USB stack.

## USB layout

| Interface | Function | Endpoints |
| --- | --- | --- |
| 0 | HID NKRO keyboard | `0x81` interrupt IN |
| 1 | MIDI Audio Control | none |
| 2 | MIDI Streaming | `0x02` OUT, `0x82` IN |
| 3 | 90-byte updater HID feature report | control endpoint only |
| 4 | CDC ACM control | `0x83` interrupt IN |
| 5 | CDC ACM data | `0x04` OUT, `0x84` IN |

The descriptors specify `1532:02b0`; successful hardware enumeration of this
revision has not been established. Interface 3 implements the Razer
90-byte command frame and accepts channel 0 / opcode `0x04` / mode 1. It writes
the bootloader's `0xaaaaaaaa` reset cookie at `0x2002fffc` and resets after a
20 ms deferral. Whether that deferral reliably follows host acknowledgment of
the control status stage remains to be verified. Device-information queries
used by `updater/` are also implemented. Flash erase/program/verify remains
bootloader-owned.

## Build

The source snapshots are from the official MCUXpresso Installer 26.06.123
catalog. The expected Arm GNU toolchain is 14.2.1, matching the catalog entry.
CMake and Ninja are also required.

```sh
# Native logic tests
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests

# LPC5528 application
cmake --preset firmware
cmake --build --preset firmware
```

Outputs are written to `build-firmware/`:

- `huntsman_firmware.elf` — symbols and debug information;
- `huntsman_firmware.hex` — addressed Intel HEX;
- `huntsman_firmware.bin` — exactly 131072 bytes for the updater.

The post-build validator rejects an incorrect image length, stack/reset
vector, VID/PID, interface ordering, endpoint layout, NKRO report, or updater
feature-report size.

## Offline ARM USB audit

```sh
python3 -m venv .venv-audit
. .venv-audit/bin/activate
python3 -m pip install -r tools/requirements-audit.txt
cmake --build --preset firmware --target audit-usb
```

This executes the actual linked NXP DCI/IP3511/class code and application
callbacks at both modeled USB speeds. It checks enumeration control transfers,
NKRO, MIDI, CDC, updater information queries, and bus reset after transfers.
USB SRAM accesses are checked for Device-memory alignment.

It does **not** test electrical behavior, CPU startup, clocks, PHY negotiation,
interrupt delivery, optical scanning, or lighting. Board clock/delay/IRQ/PHY
recovery functions are stubbed. No build or test target flashes or resets a
connected device.

## Hardware assumptions and calibration

The peripheral pins, optical transactions, I2C addresses, boot cookie, memory
layout, and updater framing are reconstructed from the supplied firmware and
updater evidence, but the reconstruction is incomplete. Production startup
queries ASIC metadata and selects 61/62/65-item profiles; the current code
assumes 61. Production contains default profile tables as well as runtime
table loading. The earlier statement that maps could not be recovered from
the application image was not justified. `src/keyboard.c` still has a
provisional ANSI 60% order and lighting has an identity LED order; neither
should be treated as recovered production mapping.

The current off-chip code also omits observed GPIO 8/26 transitions and the
primary LED-enable table, and unconditionally accesses a secondary controller
that production gates on ASIC profile 3. These must be resolved from the
production instructions before considering another hardware trial.

Optical actuation defaults to 31.25% normalized travel and releases at 21.875%.
The firmware waits for 128 complete valid scan frames before generating key
events. These constants live in `include/optical_scan.h`.

The secondary controller's own 37408-byte firmware update protocol is not
implemented in this application. The main-MCU update path is implemented: a
computer-initiated entry command is implemented but its complete hardware
round trip has not been validated with this revision.

## Flashing safety

No further device flashing, reset, or mode changes are authorized during the
current audit. Manual recovery is expensive and must not be a test strategy.
A successful build or offline USB test is not permission to flash.

After the outstanding audit issues have been resolved and the user explicitly
approves a hardware trial, use only the supplied `updater/` implementation and
the exact reviewed application image. Do not program the application binary
at address zero or overwrite the bootloader. Keep `../extracted_firmware`
read-only; do not use the old broken implementation as a reference.
