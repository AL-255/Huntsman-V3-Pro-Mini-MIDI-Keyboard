# Firmware-normalized floating-point velocity

Normalization is included in the installed `keyboard-calibration-parallel`
image with HKG6; see [current validation](CALIBRATION.md#validation-status).

The keyboard forms four intervals from five post-trigger samples, discards
the furthest median outlier and averages three intervals at assumed 8 kHz
([details](MIDI_FILTER.md)), then stores:

```text
normalized_velocity = clamp(raw_velocity / 4500000.0, 0.0, 1.0)
```

Velocities <= 0 become exactly 0.0; velocities >= 4,500,000 become exactly
1.0. Values between are linear. The register is a 32-bit float computed on
the MCU. The five-point window, 8 kHz assumption, per-key release rearming,
overlapping captures, validity and counters are unchanged. MIDI independently
converts this float to attack velocity 1–127. No physical scan-rate change is
introduced by normalization or calibration.

The GUI only decodes and formats the received float (three decimal places on
keys, six in the selected-key panel). It performs no scaling or clamping.
For older HKG2 firmware, the GUI labels signed values as legacy raw counts/s;
it does not silently normalize them on the host.

## Wire format

HKG6 packets are 1152 bytes. Offset 447 contains 65 little-endian IEEE-754
float32 normalized velocities. Completion counters start at 707 and per-key
state bytes at 967. The result-valid flag distinguishes a completed zero from
no result. The decoder rejects NaN, infinity and out-of-range floats.
See [the complete wire layout](MIDI_PROTOCOL.md#hkg6-telemetry).

## Build and tests

```sh
cmake --preset host-tests
cmake --build --preset host-tests
cmake --preset keyboard-calibration-parallel
cmake --build --preset keyboard-calibration-parallel
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
cmake --build --preset keyboard-calibration-parallel --target audit-keyboard
```

Native and compiled ARM tests cover negative/zero clamping, a small positive
fit, 4,499,200 / exactly 4,500,000 / 4,500,800 and larger fits, independent
65-key captures, overlapping windows, and float32 CDC serialization.
GUI/decoder tests cover display formatting, old-format compatibility,
invalid float rejection, and apply-all/readback behavior.
These tests are offline and do not constitute hardware validation.
