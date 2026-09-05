# Per-key velocity and apply-all thresholds

Current firmware is `keyboard-midi` with HKG4; see [MIDI design](MIDI_DESIGN.md)
and [validation](MIDI_VALIDATION.md). The checkpoints below are historical.

The next source extension, [firmware-normalized float velocity](NORMALIZED_VELOCITY.md),
uses HKG3 in a separate **unflashed** `keyboard-velocity-float` candidate.
The signed-count HKG2 format and hardware checkpoint below describe the
then-flashed image; do not rebuild that directory with newer sources.

Status: **flashed once with explicit authorization on 2026-09-05**. USB and
live HKG2 telemetry checks passed. Physical velocity capture remains unverified;
no key was pressed during the ten-second check. See hardware evidence below.

```sh
cmake --preset keyboard-velocity
cmake --build --preset keyboard-velocity
python3 tools/keyboard_gui.py --device /dev/ttyACM0
```

Use `build-keyboard-velocity/huntsman_firmware.bin` for a separately authorized
application-only flash. The previous `build-keyboard-gui` artifacts are preserved.
Restart the GUI after updating firmware. The host also accepts the previous
HKG1 firmware: it displays that velocity is unavailable and disables the new
apply-all button, while keeping ordinary monitoring/per-key edits available.

## GUI

**Apply thresholds to all keys** uses the current press/release entry fields.
After confirmation, it sends a single `cfg all ID PRESS RELEASE` command.
Firmware validates the pair and sensor count before changing anything, updates
every active sensor in one main-loop operation, increments the configuration
revision once, and clears key/velocity state once. The GUI waits for the ACK
and verifies all threshold pairs in the returned snapshot. Invalid requests
change neither configuration nor capture state. Settings remain RAM-only.

Key tiles show current raw readback and latest completed velocity (`v+…` or
`v-…`). Selecting a key shows its signed velocity, completed-fit count and
release-armed/pending state. A prior result remains displayed while a later
capture is pending; the count identifies completed captures. Disable keyboard
output while tuning if desired: **velocity capture continues with HID disabled**.
The GUI never computes actual-device velocity from its decimated snapshots.

## Data structure and scan flow

There is one `keyboard_velocity_t` per supported sensor (65 maximum):

| Storage | Purpose |
| --- | --- |
| Five uint16 samples + write index | Latest five consecutive valid scans of this key |
| Five-bit pending-trigger mask | One bit for each trigger age 0..4 scans |
| Release-armed flag | A new press may register only after raw exceeds this key's release threshold |
| Signed int32 velocity + validity flag | Latest completed fit for this key |
| uint32 completion counter | Number of completed fits, wrapping naturally |

The struct is 24 bytes on this target: 1560 bytes for all 65 sensors, no heap,
floating point, per-key shared capture buffer, or variable-size event queue.

For every valid full scan, each sensor is processed independently:

1. Write its new raw sample into its five-slot circular window.
2. If a pending trigger is five scans old, calculate its fit from this window
   and update that key's result/counter.
3. Advance trigger-age bits.
4. Rearm if `raw > release`. If its Schmitt state transitions up -> down
   (`raw < press`) and it is armed, register a new age-zero trigger and disarm.
5. Independently deliver the ordinary HID down/up transition when keyboard
   output is armed. HID key-down is not delayed for velocity acquisition.

```text
scan             trigger     +1     +2     +3     +4     +5
pending bit         0         1      2      3      4     complete
fit samples                   y1     y2     y3     y4     y5
```

The triggering sample is excluded, exactly as in the previous capture script.
A release before completion does not truncate the five-point window. It can
arm another press even while the earlier window is pending: multiple age bits
preserve overlapping captures. Each completes using its own exact next five
samples. At most one trigger per key per scan can enter this five-scan delay
line, so it cannot overflow or overwrite an uncompleted trigger. Different
keys never share sample history, arming state, pending bits or output registers.

The five-point least-squares calculation is the same as `press_velocity()` in
`tools/last_key_stream.py`:

```text
velocity = 800 × (2*y1 + y2 - y4 - 2*y5)   raw counts/second
```

It assumes samples are spaced at **8000 Hz**, as previously requested. Positive
means decreasing readback (pressing); negative means increasing readback during
the window. Zero is a valid result. Maximum magnitude for valid 1..4096 samples
is 9,828,000, safely within int32. These are not calibrated millimeters/second
or MIDI velocities. The prior physical scan measurement was roughly 1.6 kHz;
using the fixed 8 kHz assumption does not claim or establish an 8 kHz scan rate.

Startup/invalid scans, USB reset and configuration/enable changes invalidate
results, cancel all pending fits and require a new observed release for each
key. Completion counters are retained until application restart. No fit spans
an invalid scan or a configuration change. The existing global neutral guard
for HID output is separate from these independent per-key velocity gates.

## Telemetry

`stream gui` now sends **HKG2**, version 2, 1088 bytes, at most once per 33 ms.
Bytes 7..446 retain the HKG1 fields in [KEYBOARD_GUI.md](KEYBOARD_GUI.md).

| Offset | Field |
| --- | --- |
| 0 | `HKG2` |
| 4 | uint16 length = 1088 |
| 6 | Version = 2 |
| 447 | 65 signed int32 velocities |
| 707 | 65 uint32 completed-fit counters |
| 967 | 65 state bytes: release-armed=1, result-valid=2, pending=4 |
| 1032 | 52 reserved zero bytes |
| 1084 | uint32 sum of the preceding 542 little-endian uint16 words |

Unused sensor slots are zero. Signed values use little-endian two's complement.
The stable USB-owned buffer is 1088 bytes, separate from the latest unsent
snapshot. Whole/compact streaming retains its existing 640-byte batching limits
and existing semantics. Dedicated USB SRAM allocation is unchanged.

Velocity computation processes every received valid scan, not every GUI frame.
**GUI telemetry is latest-only**, not a lossless velocity event log: several
captures of a key between snapshots increment its counter, but only the latest
result is displayed. Future MIDI/event transmission is not added by this change.

## Validation and image

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
env PYTHONPATH=/tmp/huntsman-audit-python-deps cmake --build build-keyboard-velocity --target audit-lighting
```

Coverage: 65 simultaneous different slopes; equality/release gating; three
overlapping windows on one key; 16,640 randomized per-key observations checked
against a full-history oracle; invalid/config cancellation; velocity with HID
disabled; actual ARM DMA -> fits -> HKG2 readback; atomic all-key command and
invalid-request rejection; FS/HS larger-frame transport and pending-buffer
immutability; legacy decoder compatibility; GUI button/ACK/readback tests via
PTY and a private virtual display; USB/updater/lighting/stream regressions.

These tests use synthetic hardware responses and do not establish physical
press behavior or new-image stability on the board.

Image SHA-256:
`0931e4acfd3bf8bdd9a6bac6b72f0416f85a23a372924bf9e3eab4965271dd65`.
File size 131072; load image 56508 bytes; SRAMX 22280/24576 bytes; dedicated
USB SRAM 15488/16384 bytes; separately reserved stack 8192 bytes.

Preserved flashed baseline SHA-256:
`bc12508f5e3e81e50f287e4f9c4fe09690312e8b6c9891953ad6b980018328ce`.

## Authorized flash and live check (2026-09-05)

- Hash and image validator confirmed the image above before device writes.
  Used the supplied updater once: software bootloader entry, application-only
  erase/program, all 2048 64-byte program blocks acknowledged, then DFU exit.
  No independent flash readback, retry, unplugging, GPIO cycling, or writes to
  bootloader, factory or secondary-firmware regions.
- USB port `3-2.1`: application 17 -> bootloader 18 -> application 19 at
  480 Mbit/s. Keyboard, MIDI and CDC drivers bound. Updater serial query
  returned `OPENHUNTSMAN0001`. Kernel logs show the expected transitions;
  the control-only updater HID interface remains unbound as before.
- Real GUI transport connected to `ttyACM0`, selected HKG2 and received its
  configuration acknowledgment. Ten seconds produced 304 valid snapshots,
  sequence 0..303, with zero scanner/LED errors and no USB re-enumeration.
- Keyboard enabled, armed and scan-valid; all 61 pairs read back 3600/3700.
  Final raw range 3784..4012; A 3920. Neutral NKRO report; no pressed sensors
  or completed fits observed. This verifies live telemetry and idle operation,
  not physical threshold crossing or velocity accuracy.
- Check made no threshold writes or resets and closed CDC afterward. The
  keyboard remains enabled; the GUI can now connect to the port.
