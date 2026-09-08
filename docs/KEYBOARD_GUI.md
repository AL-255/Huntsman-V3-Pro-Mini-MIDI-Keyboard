# Standalone keyboard and configuration GUI

Current firmware is `keyboard-fn-menu` with HKG6 telemetry:
standalone Schmitt keyboard, MIDI, normalized per-key velocity and parallel
calibration. The GUI supports older HKG1–5 devices with version-gated controls.
See [current validation](CALIBRATION.md#validation-status).
The GUI never flashes the application or enters the bootloader; completing
calibration saves endpoints to the two authorized tail pages.

## Build and run

From the repository root, using the existing pinned NXP SDK/toolchain setup:

```sh
cmake --preset keyboard-fn-menu
cmake --build --preset keyboard-fn-menu
python3 tools/keyboard_gui.py --device /dev/ttyACM0
```

Current application: `build-keyboard-fn-menu/huntsman_firmware.bin`, exactly 131072 bytes,
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
- Default for every sensor: **press 3500, release 3600**.
- Allowed configuration: `1 <= press < release <= 4095`. Readbacks themselves
  may reach 4096. A release threshold of 4096 could never be exceeded.
- Down/up transitions use the recovered physical-key/action maps and existing
  16-byte NKRO keyboard report, including modifiers and keyboard FN actions.
  Right Alt/Menu/Right Ctrl/Right Shift send Left/Down/Right/Up; captions retain
  physical names. See [Fn keyboard shortcuts](FN_MENU.md#keyboard-shortcuts)
  for the green-hinted function and navigation layer. MIDI mappings are separate.
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

The FN+Tab/FN+Caps modal editors use the shared hold-preview/release-entry menu,
then retain the original editor event behavior. Escape exits; releasing FN alone does
not exit. A dirty **actuation** commit now converts the original normalized
press/release thresholds through each sensor's calibration into the raw
Schmitt pairs shown in the GUI. This replaces per-key custom pairs, takes effect
atomically and requires neutral before reporting resumes. Rapid-trigger editor
compatibility does not enable rapid-trigger behavior in the raw Schmitt engine.
The GUI reports editor mode; ordinary edits are rejected while editing, while
Disable keyboard cancels the pending edit. See [Fn menu](FN_MENU.md).
Consumer/media/profile actions outside the keyboard HID remain unsupported.

Fn+Enter previews the target mode and toggles on release. Fn+R previews RESET,
then opens RESET? on release. Release all keys, then press green Y to clear
saved calibration or red N to cancel. Confirmed clearing restores application
defaults once all keys are neutral. The GUI observes calibration generation 0
afterward; simply opening or cancelling confirmation does not change storage.
MIDI mapping controls use note names or
numbers; Fn, Left Ctrl/Windows/Alt, Right Alt/Ctrl and Space are reserved controls.
The GUI labels Right Alt/Ctrl octave −/+, Left Ctrl/Alt bend −/+ and Left
Windows modulation, and Space sustain. Space uses its editable Schmitt pair
to send CC64 127/0. Wheels use fixed 3800…1000 endpoints, not GUI Schmitt
thresholds or calibration bounds. A host profile assigning notes to a reserved
control is rejected before applying anything; existing files are not rewritten.
Per-key velocity is a
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
They also do not contain Fn+Left Shift's MIDI lower-row mute. This setting is RAM-only,
survives mode switches and leaves the displayed mappings intact. A Caps/Shift
row key can show an assigned note in the GUI yet be muted by Fn+Left Shift; hold the
combo to preview `LOWER-ON`, then release to restore it. `menu status` reports
the flag over text CDC; the HKG6 GUI format is unchanged.
Fn+E/Fn+S also select RAM-only MIDI root/scale filters. These are not GUI
mapping edits or JSON fields. Assigned notes can be silent/dark because of
the current filter; the GUI still shows their assignments and raw down state.
Use `menu status` for root/scale names, or T in the Fn+S menu for chromatic.

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
cmake --build --preset keyboard-fn-menu --target audit-lighting audit-calibration
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
