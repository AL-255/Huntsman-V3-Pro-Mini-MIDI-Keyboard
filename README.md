# Huntsman V3 Pro Mini MIDI Keyboard

An independent LPC5528 application for the Razer Huntsman V3 Pro Mini, using
the official NXP MCUXpresso USB and peripheral drivers. It retains the
existing bootloader and computer-initiated application updater.

**Current firmware: `keyboard-midi`, flashed once with explicit authorization
on 2026-09-05.** The application returned at USB high speed with keyboard, MIDI
and CDC drivers bound. Offline MIDI/scan/GUI tests passed; physical playing,
visible mode indicators and DAW aftertouch still require a player check.
No build or test command below flashes hardware.

## Use the keyboard

On boot, the application starts in standard NKRO keyboard mode. Optical
scanning and travel-reactive lighting start after USB configuration; the GUI
is not required for keyboard or MIDI operation.

- Press below the per-key press threshold; release above its release threshold.
  Defaults are **3600 / 3700**. Equality holds the current state.
- Hold **Fn and press Enter** to toggle keyboard ↔ MIDI mode. Release all keys
  after switching. The chord is consumed rather than sent as Enter.
- A mode change produces two whole-keyboard color pulses: **green = keyboard,
  blue = MIDI**. Enter retains a dim marker in that color. Other keys retain
  their white, travel-proportional lighting.
- In MIDI mode, **left Ctrl lowers the octave** and **left Alt raises it**.
  These act once per press. Existing held notes keep their original pitch.
- MIDI uses **channel 1**, Note On/Off, strike velocity and independent
  **polyphonic key pressure (aftertouch)**. Ordinary HID typing is suppressed
  in MIDI mode. Connect the keyboard's MIDI input to a software instrument
  that accepts channel 1 and polyphonic aftertouch.
- Unmapped keys are silent in MIDI mode. Fn and the two octave controls are
  reserved; Enter may be mapped but still participates in the mode chord.

MIDI note names in this project use **C0 = note 12; C4 = note 60**. Some music
applications display different octave labels for the same MIDI number.
The requested octave jumps in the default mapping are intentional:

| Keyboard | MIDI note | Number | Keyboard | MIDI note | Number |
| --- | --- | ---: | --- | --- | ---: |
| Tab | C0 | 12 | 1 | C#0 | 13 |
| Q | D0 | 14 | 2 | Eb0 | 15 |
| W | E0 | 16 | 4 | F#0 | 18 |
| E | F0 | 17 | 5 | Ab1 | 32 |
| R | G0 | 19 | 6 | Bb1 | 34 |
| T | A1 | 33 | 8 | C#1 | 25 |
| Y | B1 | 35 | 9 | Eb1 | 27 |
| U | C1 | 24 | - | F#1 | 30 |
| I | D1 | 26 | = | Ab2 | 44 |
| O | E1 | 28 | Backspace | Bb2 | 46 |
| P | F1 | 29 | left Ctrl | octave − | — |
| [ | G1 | 31 | left Alt | octave + | — |
| ] | A2 | 45 | | | |
| Backslash | B2 | 47 | | | |

## Configuration GUI

On Linux, with Python 3 and Tk installed:

```sh
python3 tools/keyboard_gui.py --device /dev/ttyACM0
# Offline preview; never opens the keyboard:
python3 tools/keyboard_gui.py --demo
```

Click **Connect**, then click a key on the physical ANSI layout. The GUI shows
live raw samples, down/up state, firmware-calculated velocity, current mode,
octave and device-confirmed settings.

- Set the selected key's press/release pair, then **Apply to selected key**.
- **Apply thresholds to all keys** changes every pair atomically on the MCU.
- Choose a note name, MIDI number 0–127, or **Off**, then **Apply MIDI mapping**.
  The key captions display the confirmed mapping; Fn/left Ctrl/left Alt are
  reserved controls. Note mappings can be edited in either performance mode.
- **Save profile…** exports confirmed thresholds and MIDI mappings to host JSON.
  **Load + apply profile…** disables output, applies and checks each setting,
  then restores the prior enable state. Older threshold-only JSON still loads.
- Enable/disable controls govern both keyboard and MIDI output; raw monitoring
  and velocity calculations continue. Configuration edits release output and
  require a fresh neutral frame before input resumes.

**Settings are RAM-only. Saving to device flash is not implemented.** A 1 KiB
area is reserved inside our application image, but physical flash mapping and
bootloader integrity checks remain unresolved. Stock settings, macros,
calibration and factory data are untouched. A reboot restores our defaults.
See [storage status](docs/DEVICE_CONFIG_STORAGE.md).

Close other serial monitors before connecting; only one tool should own CDC.
Use your system's serial-port permissions rather than running the GUI as root.
The graphical layout currently supports ANSI/61 keys; firmware scan/MIDI logic
also handles the recovered ISO/62 and JIS/65 layouts.

## Build from scratch

Requirements: Arm GNU bare-metal tools (`arm-none-eabi-gcc`, tested **14.2.1**),
CMake **3.21+**, Ninja, a native C compiler, Python **3.10+**, and Tk for the GUI.
NXP sources are already vendored at pinned official SDK revisions; no updater
EXE, original firmware, SDK installation or network download is needed to
compile the application.

```sh
git clone git@github.com:AL-255/Huntsman-V3-Pro-Mini-MIDI-Keyboard.git
cd Huntsman-V3-Pro-Mini-MIDI-Keyboard

cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests

cmake --preset keyboard-midi
cmake --build --preset keyboard-midi
```

Outputs in `build-keyboard-midi/`: `huntsman_firmware.elf`, `.hex` and
`.bin`. The binary is exactly **131072 bytes**. The linker and post-build
validator enforce application/config boundaries, vectors and USB descriptors.

**Use `keyboard-midi`, not `firmware`, for the complete application.**
The historical `firmware` preset is intentionally USB-only.
See [clean builds, dependencies and testing](docs/BUILDING.md).

## Scan/debug tools

```sh
# Twenty post-trigger samples, five-point velocity estimate, then exit:
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600
# Rearm above the threshold and capture again until Ctrl+C:
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600 --repeat
# See numeric and compact ANSI visualization options:
python3 tools/decode_scan_stream.py --help
```

The raw stream and compact capture modes are documented in
[scan streaming](docs/SCAN_STREAM.md) and [last-key capture](docs/LAST_KEY_STREAM.md).
Selecting a CDC display does not select keyboard/MIDI performance mode.

## Design and validation

- [MIDI state machine, encoding, timing, safety and tradeoffs](docs/MIDI_DESIGN.md)
- [HKG4 telemetry, command acknowledgments and profile format](docs/MIDI_PROTOCOL.md)
- [Build and test instructions](docs/BUILDING.md)
- [Optical/keyboard recovery](docs/KEYBOARD_RECOVERY.md)
- [Travel lighting and calibration limitations](docs/TRAVEL_LIGHTING.md)
- [Independent velocity registration](docs/KEY_VELOCITY.md) and
  [firmware normalization](docs/NORMALIZED_VELOCITY.md)
- [Earlier hardware checkpoints](docs/HARDWARE_HISTORY.md)
- [SDK source origins and licenses](third_party/ORIGINS.md)

Velocity assumes **8000 scans/s**, as requested; this is not proof of an actual
8 kHz hardware readback rate. An earlier full-stream hardware measurement was
about 1.60 kHz. Five actual subsequent samples are always used, so real elapsed
latency and the velocity scale depend on the actual scan cadence. Aftertouch
is normalized optical travel, not a calibrated force measurement.

Offline tests exercise C logic, Tk with a simulated CDC device, and the linked
ARM USB/scan/lighting paths with synthetic hardware replies. They do not prove
electrical behavior, physical LED colors, real-time throughput, DAW integration
or complete hardware recovery. See [the MIDI validation record](docs/MIDI_VALIDATION.md)
for the exact flashed hash and the limited live checks.

## Updating and safety

The USB composite device exposes NKRO HID, USB-MIDI, CDC ACM and the existing
90-byte updater HID interface (VID:PID `1532:02b0`). Use the supplied
`../updater/` application-only implementation for an explicitly authorized
hardware update. Do not use a generic programmer at address zero or treat the
RAM execution address `0x20000000` as a physical flash address.

Do not modify the bootloader, stock configuration/factory regions or secondary
optical-controller firmware. Manual bootloader recovery is expensive and is
not a test strategy. The original extraction remains read-only and is not
distributed in this repository; historical address references in design notes
are evidence, not flash-write targets. Application code is GPL-2.0; vendored
SDK files retain their upstream licenses.
