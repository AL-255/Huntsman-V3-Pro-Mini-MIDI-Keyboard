# Firmware-normalized floating-point velocity

Originally implemented in the unflashed `keyboard-velocity-float` candidate
described below. This calculation is now included in the **flashed**
`keyboard-midi` image with HKG4; see [validation](MIDI_VALIDATION.md).
The previous signed-integer and float-only build artifacts are preserved.

The keyboard performs the existing independent five-point fit, then stores:

```text
normalized_velocity = clamp(raw_velocity / 4500000.0, 0.0, 1.0)
```

Velocities <= 0 become exactly 0.0; velocities >= 4,500,000 become exactly
1.0. Values between are linear. The register is a 32-bit float computed on
the MCU. The five-point window, 8 kHz assumption, per-key release rearming,
overlapping captures, validity and counters are unchanged. No MIDI conversion
or physical scan-rate change is introduced.

The GUI only decodes and formats the received float (three decimal places on
keys, six in the selected-key panel). It performs no scaling or clamping.
Until this candidate is flashed, the updated GUI labels existing HKG2 readings
as legacy raw counts/s; it does not silently normalize them on the host.

## Wire format

HKG3, version 3, is the same 1088-byte layout as
[HKG2](KEY_VELOCITY.md#telemetry), except:

- Magic is `HKG3`, version byte is 3.
- Offset 447 contains 65 little-endian IEEE-754 float32 normalized velocities
  instead of signed int32 counts/s.
- Counters, validity/arming flags, padding and checksum retain their offsets.

Versioning prevents float bit patterns from being mistaken for integers.
The host accepts HKG1/HKG2/HKG3 and rejects NaN, infinity, or out-of-range HKG3
values. A valid flag still distinguishes a completed zero-velocity result from
no result. Per-key storage size and USB buffers are unchanged.

## Build and tests

```sh
cmake --preset keyboard-velocity-float
cmake --build --preset keyboard-velocity-float
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
env PYTHONPATH=/tmp/huntsman-audit-python-deps cmake --build build-keyboard-velocity-float --target audit-keyboard
```

Native and compiled ARM tests cover negative/zero clamping, a small positive
fit, 4,499,200 / exactly 4,500,000 / 4,500,800 and larger fits, independent
65-key captures, overlapping windows, and float32 CDC serialization.
GUI/decoder tests cover display formatting, old-format compatibility,
invalid float rejection, and apply-all/readback behavior.
These tests are offline and do not constitute hardware validation.

Candidate: `build-keyboard-velocity-float/huntsman_firmware.bin`, 131072 bytes.
SHA-256: `cf89ed23cf9caee38ca7583402497f12e36e824010022cc69731dda2bc33f6f9`.
Load size 56544 bytes; SRAMX 22280/24576; USB SRAM 15488/16384; separate 8 KiB stack.
