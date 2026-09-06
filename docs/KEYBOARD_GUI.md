# Standalone keyboard and configuration GUI

Current firmware is `keyboard-calibration-parallel` with HKG6 telemetry:
standalone Schmitt keyboard, MIDI, normalized per-key velocity and parallel
calibration. The GUI supports older HKG1–5 devices with version-gated controls.
See [current validation](CALIBRATION.md#validation-status).
The GUI never flashes the application or enters the bootloader; completing
calibration saves endpoints to the two authorized tail pages.

## Build and run

From the repository root, using the existing pinned NXP SDK/toolchain setup:

```sh
cmake --preset keyboard-calibration-parallel
cmake --build --preset keyboard-calibration-parallel
python3 tools/keyboard_gui.py --device /dev/ttyACM0
```

Current application: `build-keyboard-calibration-parallel/huntsman_firmware.bin`, exactly 131072 bytes,
linked at `0x20000000`. Original bootloader/update transport is unchanged.

The Linux GUI uses Python's standard library and Tk (`python3-tk` must be
installed). No pip packages are required. Your user needs access to the CDC
device. Close the decoder, serial terminals and other CDC readers first: the
GUI owns the stream while connected. It does not request root privileges.
`python3 tools/keyboard_gui.py --demo` previews the UI without device access.

Click **Connect**, then select a drawn key. The diagram uses the recovered
61-sensor ANSI mapping and standard 60% key positions/sizes, including the
6.25-unit spacebar. Fn is immediately right of Space, followed by right Alt;
their sensor IDs and threshold associations stay attached to
their respective keys. Each key shows its latest raw value; orange means its
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
not exit. These normalized settings **do not change the per-key raw
Schmitt pairs**. The GUI reports this mode explicitly. There is no invented
conversion between the production's calibrated rapid-trigger settings and raw
ADC counts. Consumer/media/profile actions outside the existing keyboard HID
report implementation remain unsupported.

Fn+Enter toggles keyboard/MIDI mode. MIDI mapping controls use note names or
numbers; Fn and left Ctrl/Alt are reserved controls. Per-key velocity is a
firmware-calculated 0–1 float, with an [interval pop filter](MIDI_FILTER.md).
**Apply thresholds to all keys** sends one atomic MCU update. The nominal
8 kHz velocity assumption is not a measured acquisition rate.

**Calibrate keys → device flash** starts the same routine as Fn+C in keyboard
mode. Release all keys for the 500 ms rest capture, then fully hold one or more
blue keys for one second. Each active hold is amber; completed keys are green.
The GUI shows progress, the number being held, inactivity time, saved generation
and errors. It disables ordinary edits during calibration; Cancel remains
available. Completion of all keys saves, while cancellation/5 s inactivity
discards staged results. See [calibration](CALIBRATION.md) for details.

## Profiles and persistence

Thresholds and keyboard enable state are **RAM-only**. Closing the GUI leaves
them active; unplugging/restarting restores defaults. Save/load JSON profiles
on the computer. Nothing writes bootloader, factory calibration, ASIC firmware
or unreviewed flash storage. MIDI mappings are likewise RAM-only. Calibration
endpoints alone persist on-device in the two documented unused tail pages.
Host JSON profiles do not contain calibration, performance mode or octave.

Load validates the entire ANSI profile before sending anything, disables
keyboard output, applies all 61 pairs and any version-2 MIDI mappings with individual readback, then restores
the preceding enable state. This is not an atomic transaction: a failure
cancels remaining commands, leaving confirmed changes in place and normally
leaving keyboard output disabled. Reconnect, inspect and load again explicitly.

## CDC protocol

Commands are newline-delimited ASCII; all arguments are decimal:

```text
stream gui
cfg get ID
cfg set ID SENSOR PRESS RELEASE
cfg all ID PRESS RELEASE
cfg midi ID SENSOR NOTE
cfg calibrate ID
cfg calcancel ID
cfg enable ID 0
cfg enable ID 1
```

`ID` is a nonzero uint32; `SENSOR` is the raw sensor index (0..60 for ANSI).
Well-formed IDs receive result 1 (accepted) or 2 (rejected) in telemetry.
Malformed/unparseable IDs receive no acknowledgment. Hosts must serialize
commands and wait for matching ACKs: only the last acknowledgment is retained.

`stream gui` selects latest-only 1152-byte HKG6 snapshots, at most one per 33 ms.
See [the current wire layout](MIDI_PROTOCOL.md#hkg6-telemetry) and
[calibration fields](CALIBRATION.md#gui-protocol).
Pending USB payloads remain immutable; only the unsent snapshot is replaced.
Old pending stream bytes may precede the first GUI frame after switching.
The GUI resynchronizes only before its first frame, then requires valid framing
and checksums. GUI sequence gaps are expected, unlike lossless last-key capture.

## Validation

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
cmake --build --preset keyboard-calibration-parallel --target audit-lighting audit-calibration
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
