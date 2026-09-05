# Standalone keyboard and configuration GUI

Historical HKG1/HKG2 checkpoint below. Current usage is in the root README;
the flashed `keyboard-midi` extension adds [HKG4 and MIDI mapping](MIDI_PROTOCOL.md).
Preserve old build artifacts rather than rebuilding them with current sources.

The `keyboard-gui` preset adds raw-value, per-sensor Schmitt switching to the
existing optical/lighting application. This image was **flashed once with
explicit authorization on 2026-09-05**; high-speed USB enumeration and the
updater serial query passed. See hardware validation below.
The application, GUI and tests never automatically flash or reset a device.

The subsequent checkpoint added [per-key velocity and atomic apply-all](KEY_VELOCITY.md)
in the separately built **flashed** `keyboard-velocity` image. Use that preset below
to preserve the flashed baseline. The updated GUI remains compatible with the
baseline, but its new velocity/apply-all features require the new firmware.

## Build and run

From the repository root, using the existing pinned NXP SDK/toolchain setup:

```sh
cmake --preset keyboard-velocity
cmake --build --preset keyboard-velocity
python3 tools/keyboard_gui.py --device /dev/ttyACM0
```

Current candidate: `build-keyboard-velocity/huntsman_firmware.bin`, exactly 131072 bytes,
linked at `0x20000000`. Original bootloader/update transport is unchanged.
Previously flashed build directories are not rebuilt by this preset.

The Linux GUI uses Python's standard library and Tk (`python3-tk` must be
installed). No pip packages are required. Your user needs access to the CDC
device. Close the decoder, serial terminals and other CDC readers first: the
GUI owns the stream while connected. It does not request root privileges.
`python3 tools/keyboard_gui.py --demo` previews the UI without device access.

Click **Connect**, then select a drawn key. The diagram uses the recovered
61-sensor ANSI mapping and standard 60% key positions/sizes, including the
6.25-unit spacebar. Fn is immediately right of Space, followed by right Alt,
as requested; their sensor IDs and threshold associations stay attached to
their respective keys. Restart a running GUI to load this host-only layout
change (no reflash). Each key shows its latest raw value; orange means its
sensor is in the down state. The selected-key panel shows thresholds read back
from the device, a recent-value plot and the last USB-submitted NKRO report.
An application submission is not proof that the computer received a report.
The GUI explicitly rejects ISO/JIS profile editing rather than mislabeling keys.

Use **Disable keyboard** while tuning to avoid typing into the GUI or another
application. Enter both thresholds and click **Apply to selected key**.
Confirmation requires a matching command ID, success result and matching
threshold readback. Stale/disconnected telemetry disables editing. The GUI
never silently retries a timed-out/rejected command.

## Key behavior

- Every valid scan updates every sensor independently; GUI frame rate does
  not control keyboard scanning or HID processing.
- Up -> down when `raw < press`; down -> up when `raw > release`.
  Equality and the interval between thresholds retain the previous state.
- Default for every sensor: **press 3600, release 3700**.
- Allowed configuration: `1 <= press < release <= 4095`. Readbacks themselves
  may reach 4096. A release threshold of 4096 could never be exceeded.
- Down/up transitions use the recovered physical-key/action maps and existing
  16-byte NKRO keyboard report, including modifiers and keyboard FN actions.
  Multiple keys can remain down together. Host key repeat is controlled by
  the operating system, not additional firmware down edges.
- Scanning starts automatically after USB configuration. Keyboard reporting
  is enabled by default and **does not require CDC or the GUI to be open**.
- Startup, re-enable, USB reset, invalid/stale scans and threshold changes
  clear host key state and require a fresh valid frame with **all** sensors
  strictly above their release thresholds before reporting resumes. Settings
  above an idle sensor's value can prevent arming; lower that threshold in
  the GUI. Release values must be chosen with sufficient idle margin.
- An optical transport fault releases keys and does not retry initialization
  or cycle GPIOs. A lighting-only fault does not disable a healthy scanner or
  keyboard. Busy HID transfers retry the current report on subsequent service
  calls; this is not a lossless transition-recording protocol.

The existing FN+Tab/FN+Caps modal editors remain available, with their original
entry/exit/event-consumption behavior. Escape exits; releasing FN alone does
not exit. These legacy normalized settings **do not change the new per-key raw
Schmitt pairs**. The GUI reports this mode explicitly. There is no invented
conversion between the production's calibrated rapid-trigger settings and raw
ADC counts. Consumer/media/profile actions outside the existing keyboard HID
report implementation remain unsupported.

Lighting, MIDI endpoints, CDC whole/last-key stream modes and the computer-
initiated updater remain present. No new ASIC commands, GPIO sequencing or
board initialization changes were introduced. This does not claim an 8 kHz
physical scan rate: the preceding hardware image measured roughly 1.6 kHz.

## Profiles and persistence

Thresholds and keyboard enable state are **RAM-only**. Closing the GUI leaves
them active; unplugging/restarting restores defaults. Save/load JSON profiles
on the computer. Nothing writes bootloader, factory calibration, ASIC firmware
or unreviewed flash storage. Persistent on-device configuration is not included.

Load validates the entire ANSI profile before sending anything, disables
keyboard output, applies all 61 pairs with individual readback, then restores
the preceding enable state. This is not an atomic transaction: a failure
cancels remaining commands, leaving confirmed changes in place and normally
leaving keyboard output disabled. Reconnect, inspect and load again explicitly.

## CDC protocol

Commands are newline-delimited ASCII; all arguments are decimal:

```text
stream gui
cfg get ID
cfg set ID SENSOR PRESS RELEASE
cfg enable ID 0
cfg enable ID 1
```

`ID` is a nonzero uint32; `SENSOR` is the raw sensor index (0..60 for ANSI).
Well-formed IDs receive result 1 (accepted) or 2 (rejected) in telemetry.
Malformed/unparseable IDs receive no acknowledgment. Hosts must serialize
commands and wait for matching ACKs: only the last acknowledgment is retained.

The flashed baseline's `stream gui` selects latest-only binary HKG1 snapshots,
at most one per 33 ms. The velocity candidate uses [HKG2](KEY_VELOCITY.md#telemetry).
Pending USB payloads remain immutable; only the unsent snapshot is replaced.
Old pending stream bytes may precede the first HKG1 frame after switching.
The GUI resynchronizes only before its first frame, then requires valid framing
and checksums. GUI sequence gaps are expected, unlike lossless last-key capture.

Each frame is 480 bytes, little-endian:

| Offset | Field |
| --- | --- |
| 0 | `HKG1` magic, 4 bytes |
| 4 | uint16 size = 480 |
| 6 | Version = 1 |
| 7, 8 | Layout profile, sensor count |
| 9 | Flags: enabled=1, armed=2, valid=4, scan fault=8, LED fault=16, FN=32 |
| 10, 11 | Command result, legacy FN editor mode (0/1/2) |
| 12 | uint32 telemetry sequence |
| 16 | uint32 raw threshold revision |
| 20 | uint32 last command ID |
| 24, 28 | uint32 scan errors, LED errors |
| 32 | 65 uint16 raw samples |
| 162 | 65 uint16 press thresholds |
| 292 | 65 uint16 release thresholds |
| 422 | 9-byte sensor-down bitmap, least significant bit first |
| 431 | Last USB-submitted 16-byte NKRO report |
| 447 | 29 reserved zero bytes |
| 476 | uint32 sum of the preceding 238 little-endian uint16 words |

Unused array entries and bitmap bits are zero. Profile/count 0/0 indicates no
scan layout yet. Validity must be checked separately from the retained raw
values; a fault snapshot may contain the last received samples.

## Validation

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
env PYTHONPATH=/tmp/huntsman-audit-python-deps cmake --build build-keyboard-velocity --target audit-lighting
```

The last command includes USB, keyboard, stream and lighting ARM execution
audits. Optional audit dependencies (Unicorn/pyelftools) and Xvfb for the Tk
test are not needed to run the application or GUI. All tests are offline;
synthetic ASIC/LED responses do not establish electrical behavior or physical
key/threshold usability on the board.

Coverage includes strict threshold boundaries, hysteresis noise, simultaneous
keys/modifiers, per-key edits, neutral arming, rejected configuration, no-CDC
operation, FN/keyboard actions and editor telemetry, USB-reset/invalid/timeout
releases, USB/MIDI/updater regression, pending-USB buffer ownership, FS/HS GUI
framing, JSON validation, PTY command acknowledgments and cancellation, and
real Tk geometry/selection/window-resize tests.

Flashed baseline SHA-256 (not the new velocity candidate):
`bc12508f5e3e81e50f287e4f9c4fe09690312e8b6c9891953ad6b980018328ce`.
Load image 55028 bytes; SRAMX 20232/24576 bytes; USB SRAM 15488/16384 bytes;
separate 8 KiB stack region remains reserved.

## Authorized hardware flash (2026-09-05)

- One software bootloader entry and one supplied-updater `flash_app_image`
  invocation for the SHA-256 above. All 2048 64-byte application blocks
  acknowledged; no independent flash readback is available. No secondary,
  factory or bootloader writes, retries, unplugging or GPIO cycling.
- USB port `3-2.1`: application device 15 -> bootloader 16 -> application 17.
  New application enumerated at 480 Mbit/s. Keyboard interface 0 uses
  `usbhid`, MIDI 1/2 use `snd-usb-audio`, CDC 4/5 use `cdc_acm` (`ttyACM0`).
- Updater serial query returned `OPENHUNTSMAN0001`. Kernel messages show the
  expected bootloader and application transitions. The control-only updater
  HID interface has no interrupt endpoint and remains unbound, as before.
- The initial standalone telemetry check found the user's GUI already holding
  the CDC advisory lock and correctly exited without competing for its data.
  Physical press/release behavior is not established by enumeration alone.
