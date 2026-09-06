# Per-key optical calibration

In keyboard mode, hold **Fn+C**, or connect the GUI and choose **Calibrate keys
→ device flash**. Keyboard output pauses for the routine. Physical entry
requires keyboard output enabled and armed; GUI entry also works when disabled.

1. Release all keys, including Fn+C. Purple indicates the release/settle phase.
   Keep the keyboard untouched for 500 ms. The next valid whole scan supplies
   each key's upper/rest endpoint.
2. Blue means not yet calibrated. Fully bottom out one or more blue keys and
   hold them for one second. Each held key turns amber independently, then
   green when registered. You may hold several together, start them at different
   times, and leave green keys held while pressing others. Include Fn, modifiers
   and space. Only the initial rest capture requires all keys released together.
3. After every key is green, the device saves and resumes operation once keys
   are released. Green remains briefly as confirmation. The GUI reports the
   saved generation. Red indicates cancellation, timeout or failure.

Five seconds without progress aborts and discards the staged result. **Cancel
calibration** in the GUI also discards it. Scan/USB failure aborts. Existing
active calibration remains unchanged on failure; no partial result is saved.
Configuration edits are rejected while the routine is active.

## Measurement choices

Readback decreases with force. Rest values must be at least 2048. Each candidate
must be at most half its own rest value. This rejects shallow touches, but cannot
prove the key has reached its mechanical stop: the user must fully press it.
Motion over 64 counts from a key's hold anchor restarts only that key's timer.
Releasing a pending key also resets only its own hold. Each key has separate
start time, anchor, sum and sample count; the mean during its stable one-second
hold supplies its lower endpoint. Already completed keys cannot register twice.
Noise alone does not indefinitely reset inactivity.

The routine stages separate endpoint arrays, a nine-byte completion bitmap,
and 65 independent hold registers. This 1324-byte state lives in a dedicated
writable section inside the existing application RAM image (0x20000000 region),
initialized explicitly at startup. It does not consume peripheral SRAMX or USB
RAM, share velocity buffers, or change the flash calibration record format.
It does not change active bounds until a complete save passes readback.
New bounds drive linear travel lighting and MIDI aftertouch. Schmitt thresholds
remain raw ADC values; calibration does not silently change press/release
settings or velocity scaling. Thresholds and MIDI mappings are still RAM-only
with host JSON import/export. Calibration endpoints alone persist on-device.

## Persistence

Only physical pages **0x7d400 and 0x7d600** are writable. Both were independently
verified as FF inside an original free allocator block. The primary settings,
serial number, bootloader and application image are untouched by calibration.
See [record format, original-driver evidence and power-failure behavior](DEVICE_CONFIG_STORAGE.md).
Stock firmware may reclaim this previously unused space; keep private backups.

## GUI protocol

`cfg calibrate ID` and `cfg calcancel ID` use the existing decimal nonzero
request ID and ACK result (1 accepted, 2 rejected). Start ACK means the routine
started, **not** that flash was saved. Observe terminal state and generation.
Cancellation is idempotent. One outstanding request is supported.

HKG6 packets are 1152 bytes, with the following calibration status fields:

| Offset | Little-endian field |
| --- | --- |
| 1112–1115 | uint8 state, completed count, selected sensor (255 none), flags |
| 1116, 1118 | uint16 hold elapsed ms (0–1000), inactivity remaining ms (0–5000) |
| 1120 | 9-byte completed bitmap, sensor order |
| 1129 | reason: 0 none, 1 timeout, 2 invalid scan/USB, 3 cancelled, 4 storage |
| 1130, 1132 | selected candidate upper/lower uint16 endpoints; zero if absent |
| 1134 | reserved uint16 zero |
| 1136, 1140 | uint32 saved generation, storage error |
| 1144 | reserved uint32 zero |
| 1148 | unchanged uint32 checksum of preceding uint16 words |

HKG6 additionally uses bit 3 (value 8) of each per-key state byte at 967+i
to indicate an active calibration hold. Velocity bits 0–2 retain their meaning.
This bit is clear for completed keys and outside collection. The GUI colors
every active hold amber and displays their count. The selected-sensor/elapsed
fields show one representative hold (lowest sensor index), not a shared timer.

States: 0 idle, 1 release, 2 settle, 3 collect, 4 legacy wait key release (not
emitted by parallel calibration), 5 save,
6 complete, 7 discarded, 8 storage failure. Flags: bit0 active, bit1 a valid
saved record loaded/written, bit2 firmware supports calibration. Pending values
are not active calibration. GUI telemetry remains latest-frame, about 30 Hz;
acquisition consumes hardware scans, not GUI frames.

## Validation status

Native and modeled ARM tests are provided, including original instruction
differentials for the flash controller. These do not prove physical erase
timing or real key holding. A complete physical calibration requires pressing
all keys on the board; do not describe simulated acquisitions as hardware tests.

Installed 2026-09-05: `build-keyboard-calibration-parallel/huntsman_firmware.bin`,
SHA256 `6639660b99e8182183304c3939563d14c4243bedfbead3986826cd164aea3084`.
Application load 67948 bytes, SRAMX 23816/24576, USB RAM 15488/16384, separate
8 KiB stack. All nine host suites, Tk/PTY UI, compiled USB/keyboard/MIDI/stream
regressions and original-controller differential/calibration tests passed.

The authorized updater accepted all 2048 packets and returned at 480 Mbit/s.
Two independent physical reads of the entire application matched the build.
Readback verification of the application caused no unexpected USB reset;
HKG6 telemetry reported valid 61-key scans without optical, lighting or MIDI
errors.

### Completed physical calibration and retrieval

The user completed a calibration run on this build. Read-only retrieval at
2026-09-05 10:03:08 UTC reported state 6 (complete), 61/61 registered keys,
saved generation 1, reason/error zero, and no optical, LED or MIDI errors.
Two independent reads of 0x7d400..0x7d800 matched with no ECC/read holes.
Slot A at 0x7d400 held a valid layout-1/61-key HKC1 record with verified CRC;
slot B at 0x7d600 remained entirely FF. Status before/after agreed on generation.

Released endpoints ranged from 3784 to 4016; pressed endpoints ranged from
197 (Y) to 1269 (Escape). The next-lowest pressed value was 231 (/). These
are measured optical counts, not evidence of a fault or physical force units.
Complete per-key CSV/JSON and raw acquisition remain private and Git-ignored.
This validates one physical acquisition/program/readback, not power-failure
recovery, endurance or cold-boot loading; those latter paths have offline tests
where documented, not a performed physical power-cycle test.
