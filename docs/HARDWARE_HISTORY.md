# Historical development checkpoints

This is the README snapshot preceding the unflashed MIDI performance candidate.
Statements such as “latest” below describe their historical checkpoint, not
current build selection. Use the root README for current instructions.

# Huntsman V3 Pro Mini MIDI firmware

An independent LPC5528 application for the Razer Huntsman V3 Pro Mini. It is
designed for the keyboard's existing bootloader and the 128 KiB application
image linked at `0x20000000`; the bootloader is neither included nor modified.

Application-only configuration space is now reserved in the final 1 KiB of the
image. **Device saving is not enabled** pending verification of backing-flash
mapping and bootloader integrity checks; stock settings remain untouched.
See [device configuration storage](docs/DEVICE_CONFIG_STORAGE.md) and the separate
`keyboard-storage-layout` build preset.

The **`keyboard-velocity-float` candidate (not flashed)** normalizes velocity
on the MCU to a float in 0.0–1.0, clamping raw counts/s at 0 and 4,500,000.
The GUI displays the received float without converting it. Build separately
in `build-keyboard-velocity-float`; see [normalized velocity](docs/NORMALIZED_VELOCITY.md).

The **`keyboard-velocity` image (flashed once on 2026-09-05)** adds an atomic
**Apply thresholds to all keys** button and on-device, independent five-point
velocity registration for every key. The GUI displays latest velocities and
capture counts; each key rearms above its own release threshold. See
[velocity structure, scan flow, protocol and validation](docs/KEY_VELOCITY.md).
Its ten-second live HKG2 telemetry check passed with 304 valid snapshots and
zero scanner/LED errors. No physical press occurred during that check.

New **`keyboard-gui` image (flashed once on 2026-09-05)**: standalone NKRO keyboard
with independent raw press/release thresholds per key (defaults 3600/3700),
plus a graphical ANSI 61-key monitor/configurator. Run
`python3 tools/keyboard_gui.py --device /dev/ttyACM0`, or add `--demo` for a
device-free preview. Settings are RAM-only
with host JSON save/load; keyboard operation does not depend on the GUI/CDC.
See [keyboard GUI, behavior, protocol and validation](docs/KEYBOARD_GUI.md).
The following paragraphs describe earlier hardware checkpoints.

Hardware checkpoint: **USB bring-up only**. A single authorized flash on 2026-09-05
enumerated at high speed on Linux with keyboard, MIDI, and CDC drivers bound.
CDC emitted six `USB service alive` messages during a six-second read, and
the supplied updater's version/serial queries succeeded. This is a USB
bring-up checkpoint, not validation of keyboard scanning or MIDI traffic.
The default `HUNTSMAN_USB_ONLY=ON` build uses `src/main_usb.c`; optical and lighting code
is not linked or executed. It sends neutral keyboard reports, with MIDI
endpoints and the updater HID interface present. Periodic CDC heartbeats
have been removed from both entry points in the current source.
See [the USB-only audit](docs/USB_ONLY_AUDIT.md) for the two reproduced
alignment faults, production PHY comparison, tests, and remaining limits.

The keyboard candidate adds the recovered optical scan/key engine and FN
configuration editors. It was flashed once after explicit authorization;
software bootloader entry, high-speed re-enumeration and live isolated CDC
editor tests passed. Physical optical readbacks were subsequently tested with
the streaming image below; physical key/editor behavior remains unvalidated.
Build it with `keyboard-diagnostics`;
the default USB-only preset remains separate. ASIC initialization and host
keystrokes are explicitly CDC-gated. See [keyboard recovery and validation](docs/KEYBOARD_RECOVERY.md)
for production handler addresses, commands, tests, and incomplete features.

The latest revision streams whole-keyboard uint16 readbacks over CDC instead
of periodic status text. It was flashed once with authorization; high-speed USB,
CDC commands, and quiet idle output passed. A subsequent live CDC test started
the scanner once and received complete 61-sensor frames at about **1.60 kHz**,
with zero sequence gaps or invalid samples during the measured capture.
**The requested 8 kHz is not achieved.** Scanning/streaming were left enabled
for this running session; host keystrokes remain disabled. After a reboot,
scanning still requires `scan start`.
Use the separate `scan-stream`
preset to preserve the flashed build artifacts. Binary framing, host decoding,
8 kHz target and physical timing limits are in [CDC scan streaming](docs/SCAN_STREAM.md).

The new `travel-lighting` preset was **flashed once and hardware-smoke-tested**
on 2026-09-05. Its behavior is retained in the newer running `last-key` build.
It automatically starts optical scanning after USB configuration
and drives each key's white PWM linearly over its production-normalized optical
range, using recovered LED channel maps and controller programs. Physical
travel calibration and visible illumination remain unverified; the board still
reports no accepted ASIC calibration pairs. CDC confirmed increasing lighting
and scan counters with zero errors, plus a gap-free 61-sensor capture.
See [travel lighting recovery and validation](docs/TRAVEL_LIGHTING.md)
for the exact image, behavior, build commands, evidence and limitations.

The `last-key` preset adds host-commanded compact readbacks to the lighting
application (flashed once and hardware-tested on 2026-09-05). Run
`python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3800`
to select it. The host prints `Capture Armed`, identifies the triggering key,
prints its next 20 readbacks and a five-point velocity fit assuming 8 kHz,
then exits. Add `--repeat` to wait for release above the threshold, print
`Capture Armed` again, and capture the next press until Ctrl-C.
Key changes warn and restart the capture; detected data loss or buffer overflow
still fail. No additional flash is needed for this
host-side capture behavior. See
[last-key streaming](docs/LAST_KEY_STREAM.md) for protocol and limitations.

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
