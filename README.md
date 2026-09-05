# Huntsman V3 Pro Mini MIDI firmware

An independent LPC5528 application for the Razer Huntsman V3 Pro Mini. It is
designed for the keyboard's existing bootloader and the 128 KiB application
image linked at `0x20000000`; the bootloader is neither included nor modified.

Hardware checkpoint: **USB bring-up only**. A single authorized flash on 2026-09-05
enumerated at high speed on Linux with keyboard, MIDI, and CDC drivers bound.
CDC emitted six `USB service alive` messages during a six-second read, and
the supplied updater's version/serial queries succeeded. This is a USB
bring-up checkpoint, not validation of keyboard scanning or MIDI traffic.
The default `HUNTSMAN_USB_ONLY=ON` build uses `src/main_usb.c`; optical and lighting code
is not linked or executed. It sends neutral keyboard reports and a CDC
heartbeat, with MIDI endpoints and the updater HID interface present.
See [the USB-only audit](docs/USB_ONLY_AUDIT.md) for the two reproduced
alignment faults, production PHY comparison, tests, and remaining limits.

The keyboard candidate adds the recovered optical scan/key engine and FN
configuration editors. It was flashed once after explicit authorization;
software bootloader entry, high-speed re-enumeration and live isolated CDC
editor tests passed. Physical optical scanning remains unvalidated and off.
Build it with `keyboard-diagnostics`;
the default USB-only preset remains separate. ASIC initialization and host
keystrokes are explicitly CDC-gated. See [keyboard recovery and validation](docs/KEYBOARD_RECOVERY.md)
for production handler addresses, commands, tests, and incomplete features.

The USB-only image contains:

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

The device enumerates as `1532:02b0`. Interface 3 implements the Razer
90-byte command frame and accepts channel 0 / opcode `0x04` / mode 1. It writes
the bootloader's `0xaaaaaaaa` reset cookie at `0x2002fffc` and resets after a
20 ms deferral starting only after EP0 IN status completion. A new SETUP or
bus reset before that acknowledgment cancels entry. Those cases are tested
offline; software entry from the USB-only checkpoint and the application-only
update to the keyboard candidate subsequently passed on hardware.
Device-information queries used by the supplied
`../updater/` succeeded on hardware. Flash erase/program remains
bootloader-owned; the trial checked program acknowledgments, not readback.

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
USB SRAM accesses are checked for Device-memory alignment. Separate tests
execute startup/core-clock/USB-clock/timer setup and the PHY chirp routine.

These are limited register models: they do **not** test electrical behavior,
clock lock, PHY negotiation, or real interrupt delivery. The transport test
stubs board initialization; the startup and chirp tests cover those paths
separately under explicit model assumptions. No build or test target flashes
or resets a connected device.

## Retained legacy peripheral code

`src/main.c`, `src/optical_hw.c`, `src/optical_scan.c`, and `src/lighting.c`
retain an incomplete earlier reconstruction. Neither the USB-only nor the
keyboard-diagnostics entry point uses that peripheral path. Its guessed
calibration and lighting order must not be treated as production evidence.
The new keyboard path uses the recovered 61/62/65-sensor maps and replaces
the provisional mapping formerly in `src/keyboard.c`.

The secondary controller's own 37408-byte firmware update protocol is not
implemented in this application. The main-MCU update path is implemented: a
computer-initiated entry command is implemented but its complete hardware
round trip has not been validated with this revision.

## Flashing safety

The authorized trial is complete. No further device flashing, reset, or mode
changes are authorized. Manual recovery is expensive and must not be a test
strategy.
A successful build or offline USB test is not permission to flash.

If the user explicitly approves another hardware trial after review, use only
the supplied `../updater/` implementation and the exact reviewed application
image. Do not program the application binary
at address zero or overwrite the bootloader. Keep `../extracted_firmware`
read-only; do not use the old broken implementation as a reference.
