# Huntsman V3 Pro Mini MIDI Keyboard

An independent LPC5528 application for the Razer Huntsman V3 Pro Mini, using
the official NXP MCUXpresso USB and peripheral drivers. It retains the
existing bootloader and computer-initiated application updater.

**Current installed image: `keyboard-calibration-parallel`.** It includes the
43-note C4–B6 mapping, four-interval velocity pop filter, octave blink indicators
and a read-only CDC flash dumper. Installed and hardware-checked on 2026-09-05;
the application flash readback matches the build exactly. Bootloader readback
is partial (its first 304 bytes return ECC errors); stock configuration backup
is complete. See [private flash acquisition](docs/FLASH_DUMP.md).
No build or test command below flashes hardware.
The firmware supports simultaneous independent key holds during Fn+C /
GUI calibration. Persistence remains restricted to the two verified unused
tail pages, preserving serial-number storage. A complete user-operated 61-key
calibration has been saved and independently retrieved as generation 1, with
valid CRC and two matching reads. See [the documentation guide](docs/README.md).

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
  Left Ctrl blinks amber for a negative shift; Left Alt blinks amber for a
  positive shift. Larger shifts blink faster, from a 1.2-second cycle at ±1
  to a 0.12-second cycle at ±10. Zero shift restores normal travel lighting.
- MIDI uses **channel 1**, Note On/Off, strike velocity and independent
  **polyphonic key pressure (aftertouch)**. Ordinary HID typing is suppressed
  in MIDI mode. Connect the keyboard's MIDI input to a software instrument
  that accepts channel 1 and polyphonic aftertouch.
- Unmapped keys are silent in MIDI mode. Fn and the two octave controls are
  reserved; Enter may be mapped but still participates in the mode chord.

MIDI note names in this project use **C0 = note 12; C4 = note 60**. Some music
applications display different octave labels for the same MIDI number.
The default mapping is:

| Keyboard | MIDI note | Number | Keyboard | MIDI note | Number |
| --- | --- | ---: | --- | --- | ---: |
| Tab | C5 | 72 | Left Shift | C4 | 60 |
| 1 | C#5 | 73 | A | C#4 | 61 |
| Q | D5 | 74 | Z | D4 | 62 |
| 2 | D#5 | 75 | S | D#4 | 63 |
| W | E5 | 76 | X | E4 | 64 |
| E | F5 | 77 | C | F4 | 65 |
| 4 | F#5 | 78 | F | F#4 | 66 |
| R | G5 | 79 | V | G4 | 67 |
| 5 | G#5 | 80 | G | G#4 | 68 |
| T | A5 | 81 | B | A4 | 69 |
| 6 | A#5 | 82 | H | A#4 | 70 |
| Y | B5 | 83 | N | B4 | 71 |
| U | C6 | 84 | M | C5 | 72 |
| 8 | C#6 | 85 | K | C#5 | 73 |
| I | D6 | 86 | , | D5 | 74 |
| 9 | D#6 | 87 | L | D#5 | 75 |
| O | E6 | 88 | . | E5 | 76 |
| P | F6 | 89 | / | F5 | 77 |
| - | F#6 | 90 | ' | F#5 | 78 |
| [ | G6 | 91 | left Ctrl | octave − | — |
| = | G#6 | 92 | left Alt | octave + | — |
| ] | A6 | 93 | | | |
| Backspace | A#6 | 94 | | | |
| Backslash | B6 | 95 | | | |

Left Shift remains a normal modifier in keyboard mode. All mappings remain
GUI-editable except Fn and the octave controls. Loading a JSON profile
can overwrite these defaults with that profile's saved mappings.

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

**Calibration endpoints save to the device; thresholds and MIDI mappings
remain RAM-only.** In keyboard mode use **Fn+C** or the GUI's **Calibrate keys
→ device flash** button. Release all keys; wait 500 ms (purple → blue), then
fully press and hold blue keys for one second until green. Multiple keys can
be held together; moving/releasing one does not reset the others. Green keys
may stay held while you calibrate the rest. Include Fn and modifiers. Five seconds
of inactivity or GUI cancellation discards the attempt. Completion saves all
endpoints; the GUI shows progress and saved generation.

Only pages **0x7d400 and 0x7d600**, independently verified unused and FF-filled,
are write targets. Serial-number/primary settings pages are never erased.
The application-image reservation remains unused. Returning to stock may
reclaim the tail space and discard custom calibration.
See [calibration instructions](docs/CALIBRATION.md) and [storage design](docs/DEVICE_CONFIG_STORAGE.md).

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

cmake --preset keyboard-calibration-parallel
cmake --build --preset keyboard-calibration-parallel
```

Outputs in `build-keyboard-calibration-parallel/`: `huntsman_firmware.elf`, `.hex` and
`.bin`. The binary is exactly **131072 bytes**. The linker and post-build
validator enforce application/config boundaries, vectors and USB descriptors.

**Use `keyboard-calibration-parallel`, not `firmware`, for the complete application.**
The `firmware` preset is intentionally USB-only.
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
- [MIDI mapping and interval pop-filter behavior](docs/MIDI_FILTER.md)
- [HKG6 telemetry, command acknowledgments and profile format](docs/MIDI_PROTOCOL.md)
- [Parallel calibration and its physical save/readback record](docs/CALIBRATION.md)
- [Tail-page storage and serial-number protection](docs/DEVICE_CONFIG_STORAGE.md)
- [Build and test instructions](docs/BUILDING.md)
- [Optical/keyboard recovery](docs/KEYBOARD_RECOVERY.md)
- [Travel lighting and calibration limitations](docs/TRAVEL_LIGHTING.md)
- [Independent velocity registration](docs/KEY_VELOCITY.md) and
  [firmware normalization](docs/NORMALIZED_VELOCITY.md)
- [USB integration and safety](docs/USB_DESIGN.md)
- [SDK source origins and licenses](third_party/ORIGINS.md)

Velocity forms four signed differences from five post-trigger samples,
discards the interval furthest from their median, and averages the other three
before firmware-side 0…1 normalization. Ties discard the earliest interval.
The host capture tool uses the same estimator and prints fractional counts/s.
See [filter details and limitations](docs/MIDI_FILTER.md).

Velocity assumes **8000 scans/s**, as requested; this is not proof of an actual
8 kHz hardware readback rate. Five actual subsequent samples are always used,
so real elapsed
latency and the velocity scale depend on the actual scan cadence. Aftertouch
is normalized optical travel, not a calibrated force measurement.

Offline tests exercise C logic, Tk with a simulated CDC device, and the linked
ARM USB/scan/lighting paths with synthetic hardware replies. They do not prove
electrical behavior, physical LED colors, real-time throughput, DAW integration
or complete hardware recovery. See [current validation](docs/CALIBRATION.md)
for the installed hash, live checks and remaining verification limits.

## Updating and safety

The USB composite device exposes NKRO HID, USB-MIDI, CDC ACM and the existing
90-byte updater HID interface (VID:PID `1532:02b0`). Use the supplied
`../updater/` application-only implementation for an explicitly authorized
hardware update. Do not use a generic programmer at address zero or treat the
RAM execution address `0x20000000` as a physical flash address.

Do not modify the bootloader, serial-number/primary settings, factory/security
regions or secondary optical-controller firmware. Calibration owns only the
two documented tail pages; there is no arbitrary flash-write command.
Manual bootloader recovery is expensive and is
not a test strategy. The original extraction remains read-only and is not
distributed in this repository; original-firmware address references in design notes
are evidence, not flash-write targets. Application code is GPL-2.0; vendored
SDK files retain their upstream licenses.
