# Whole-keyboard CDC scan stream

This revision was **flashed once after explicit authorization**. High-speed USB,
CDC commands, and quiet idle output passed. A subsequent physical test received
61-sensor frames at approximately **1.60 kHz, not the requested 8 kHz** (details
below). The periodic
`USB service alive` message (called “USB service active” in the request)
is removed from both USB-only and keyboard entry points.

## Operation

ASIC initialization is still explicit: `scan start`. By default, every newly
accepted A0 scan frame then produces one binary CDC record. No text-formatted
per-sensor loop, normalized values, interpolation, or repeated snapshots are
substituted for raw uint16 readbacks. Values are in ASIC sensor-index order,
with the metadata-selected 61/62/65 count; mapping is documented in
[KEYBOARD_RECOVERY.md](KEYBOARD_RECOVERY.md).

- `stream off`: stop binary output, keep the ASIC scanning, resume text commands.
- `stream on`: resume binary output on the next fresh scan; does not initialize
  the ASIC, reset anything or enable host keystrokes.
- `scan stop`: stop streaming and quarantine the scanner as before.
- Text logs/command responses are suppressed while streaming. Commands still
  execute; send `stream off` before `scan status` or other text diagnostics.
- Host keyboard output remains independently gated by `keys on`.

There may be existing text/an in-flight text transfer before the first binary
record. The decoder resynchronizes on validated frame boundaries. Once binary
streaming owns CDC IN, text cannot be interleaved into its frames. After
`stream off`, an already-submitted binary batch finishes before queued text.

## Rate and buffering

The target is 8,000 complete keyboard records per second over high-speed CDC:
160 bytes × 8,000 = **1.28 MB/s**. The queue holds 32 records (4 ms at target
rate), and USB submissions batch up to four records/640 bytes. Dedicated SRAMX
buffers remain immutable while USB owns them; USB SRAM allocation is unchanged.
USB backpressure never blocks the scanner, watchdog or main loop. Entire new
records are dropped when the queue is full; there are no truncated records.
Disconnect/reset/explicit stream stop also discard queued records.

**The measured fresh hardware readback rate is about 1.60 kHz, not 8 kHz.** Production SPI is
8 MHz. Two header bytes plus 61/62/65 uint16 samples take at least 124/126/132 µs
on the wire, versus a 125 µs target period, before software/peripheral overhead.
The existing CTIMER2 scheduler retains its nominal 125 µs period. No unverified
SPI overclock or ASIC protocol change was introduced. Thus 62/65-key layouts
cannot reach 8 kHz unique full scans at that SPI rate, and even 61 has very
little margin. Full-speed USB likewise cannot be assumed to sustain this rate.

Use record ticks/sequence gaps to measure the achieved rate, not the nominal
timer or CDC baud setting. The line-coding baud is not a physical UART limit.
Dropped counters track enqueue failures and discarded queued records; they
are not proof of host receipt. USB cancellation/disconnect may lose an
in-flight batch too; sequence gaps expose missing observations.

## Wire format

Each record is exactly 160 bytes. All multi-byte values are little-endian.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `HKS1` (format/version magic) |
| 4 | uint16 | Record size, 160 |
| 6 | uint8 | Sensor count, 61/62/65 |
| 7 | uint8 | ASIC layout, 1/2/3 |
| 8 | uint32 | Stream observation sequence; advances even on queue drops |
| 12 | uint32 | CTIMER2 tick when main observes the readback, nominal 125 µs/tick |
| 16 | uint32 | Cumulative enqueue/discard drop count |
| 20 | uint16 | Bit 0: one or more raw values outside production-valid 1–4096 |
| 22 | uint16 | Reserved, zero |
| 24 | uint16[count] | Every sensor's unmodified 16-bit ADC readback |
| after samples through 155 | bytes | Zero padding, not extra sensors |
| 156 | uint32 | Sum of the 78 little-endian uint16 words in bytes 0–155 |

The checksum detects framing/capture errors; it is not a CRC or authentication.
Ticks timestamp observation, not an independently measured ADC acquisition
instant. Invalid ADC values are retained and flagged, not silently clamped.
Non-A0 markers and the two production startup-discard frames are not samples.

## Decode and inspect

Capture CDC in raw mode to a binary file with a serial client. Avoid terminal
echo and newline transformations; drain/resynchronize on opening. Then:

```sh
python3 tools/decode_scan_stream.py capture.bin --hex
python3 tools/decode_scan_stream.py capture.bin --summary
# Or pipe already-raw CDC bytes to the decoder:
python3 tools/decode_scan_stream.py --hex < capture.bin
```

Rows are `sequence,tick,dropped,flags,raw0,...`. Without `--hex`, samples are
unsigned decimal, right-aligned in five-character fields (including the full
uint16 range); counters occupy ten characters. With `--hex`, samples use
four-digit hex. The decoder accepts arbitrary
USB/read chunk boundaries and skips malformed records/text. Terminal rendering
8,000 long rows/s may bottleneck the host; binary capture is preferable.

## Build/validation

```sh
cmake --preset scan-stream
cmake --build --preset scan-stream
cmake --build --preset scan-stream --target audit-keyboard
```

The audit uses the optional Python dependencies in `tools/requirements-audit.txt`
and the read-only production image as described in KEYBOARD_RECOVERY.md.
Output is `build-stream/huntsman_firmware.bin`; the existing flashed
`build-keyboard` and USB-only `build-firmware` artifacts are preserved.
The reviewed streaming build is 131072 bytes, SHA-256
`64be8948ae79990a402be9dd4f10b32d94510f5d8d4f25527e35c2d4f5053466`.
SRAMX use including heap is 18120/24576 bytes; USB SRAM remains 15488/16384.

Offline checks execute actual ARM CDC/USB code with a synthetic source of
8,000 full records and four records per modeled 500 µs host-service interval.
They check all 16 sample bits, checksums, multi-packet assembly, bounded
backpressure, whole-record loss counters, immutable in-flight data, text
arbitration, reset recovery, and synthetic ASIC-to-CDC integration. This
validates software framing/data flow, **not real elapsed throughput or ASIC
rate**. The model was corrected to consume double-buffered USB packets in
EPINUSE order; choosing the first active buffer had reordered long transfers.
Full USB and keyboard regression audits run with the corrected model.

## Authorized hardware flash

The user subsequently requested `flash`. The exact 131072-byte hash above,
from source commit `64330b0`, was verified before a single software update
using the supplied updater. On the same USB port `3-2.1`, application device
9 became bootloader device 10, then application device 11. Every program block
was acknowledged; no readback was performed. There was no manual unplug,
forced boot mode, retry, second flash, bootloader write or secondary update.

The application enumerated at 480 Mbit/s. Keyboard, MIDI and CDC drivers bound;
`/dev/ttyACM0` responded to status/help, including the new stream commands.
After initial raw-terminal resynchronization, status showed `phase=0`,
`transfers=0`, `stream_dropped=0` and `host=0`. A subsequent three-second
idle read returned zero bytes, confirming the heartbeat was gone. No
`scan start`, `keys on` or ASIC command was sent.

Dmesg recorded bootloader enumeration at monotonic 89320.953549 and application
enumeration at 89337.021726. The known control-only HID endpoint, MIDI fallback
and usbfs interface-claim warnings remained; no enumeration failure appeared.
Device number 11 remained unchanged through CDC validation. This establishes
flash/USB/command success, not live raw sampling or an achieved 8 kHz rate.

## Subsequent live CDC/ASIC test (2026-09-05)

After the user reported no output, direct raw-mode reads confirmed idle CDC
was silent and status showed `phase=0 transfers=0 frames=0`: the scanner had
never been started. The initial flash checks did not validate the requested
scan stream. One `scan start` was then sent, with host keystrokes kept off.
There was no reflash, MCU reset, scanner retry or manual reconnect.

The first six-second capture received 9,353 checksum-valid HKS1 records, each
containing 61 raw uint16 values, with zero sequence gaps. Status subsequently
reported `phase=8 profile=1 count=61 errors=0 settled=1 valid=1 calibrated=0`.
The zero calibration count means this test does not establish recovered
per-key calibration or physical keyboard/editor correctness.

A second six-second capture received 1,544,320 bytes / 9,652 valid records,
with zero sequence gaps and zero invalid-sample flags. The drop counter was
unchanged throughout capture. Readback tick intervals were 5 ticks for 9,477
adjacent pairs, 4 for 127 and 6 for 47: 1,602.66 records/s by nominal timer
ticks. Host elapsed capture rate was 1,608.47 records/s, including buffering.
With binary streaming disabled but acquisition still running, status counter
deltas measured 1,605.19 frames/s. Thus disabling CDC streaming did not
materially improve acquisition rate; the specific acquisition bottleneck is
not yet established. Do not describe this as an 8 kHz hardware pass.

Drops accumulated while no host reader was draining CDC, as expected from the
bounded queue, and stopping the stream also discards queued records. These
are distinct from the zero gaps/drop-counter growth during the measured
continuous capture. USB remained application device 11 on `3-2.1`, with no
new USB/kernel fault messages during testing. Scanner and stream were left
enabled, host keystrokes off; this does not change the boot-time OFF default.

The stream is binary, not terminal-readable lines. From the repository root,
with permission to access the serial device, inspect the running stream using:

```sh
python3 -u tools/decode_scan_stream.py --live /dev/ttyACM0
```

Do not run two readers concurrently. To inspect text status instead, send
`stream off` before `scan status`; `stream on` resumes without reinitializing
the running scanner.

Live mode configures raw tty input and discards existing host tty input on
opening. It continuously drains/validates input on an independent reader
thread, retaining only one latest-report slot. The display samples that slot
at most 20 times/s (`--rate` overrides this), discarding intervening reports.
A slow stdout consumer does not stop serial draining or create a display
FIFO; there is no catch-up burst or repeat of an unchanged report. Reports
older than 250 ms since host reception (or two display intervals, whichever
is longer) are not newly printed. `--duration 5` provides a bounded test and
prints received/displayed counts to stderr. Ordinary file decoding without
`--live` still preserves all records.

"Latest" means the newest complete, validated report received by the host
when a row is selected. USB/firmware buffers, an in-flight transfer and the
terminal's own rendering have unavoidable latency; this is not a guarantee
of the sensor state at the instant pixels appear. The existing firmware's
transport queue is unchanged, and no firmware flash is needed for this
host-side display change.

Hardware validation of `--live --duration 3` received 4,842 records and printed
60 rows, omitting 4,782 records from display. Adjacent displayed sequences
jumped by 80 or 81. All rows had 61 decimal sample fields of width five;
the firmware drop counter stayed constant during this capture. USB device
number remained 11. The blocked-output host test independently verifies that
100 intervening reports are replaced by the newest report, not replayed.

## Labelled ANSI block display

```sh
python3 -u tools/decode_scan_stream.py --bars /dev/ttyACM0
# Narrow terminal: start the visible window at raw sensor index 40.
python3 -u tools/decode_scan_stream.py --bars --start 40 /dev/ttyACM0
```

`--bars` implies latest-only live mode, including the independent draining
reader and default 20 Hz display cap. It redraws just two terminal rows in
place: a caption with each key name, then its colored block directly below.
No numeric sample row or scrolling history is emitted. The scale is fixed:
raw 1–4096 spans eight equal bins from blue `▁` to red `█`, with cyan/green/
yellow intermediate levels. Invalid raw values appear as magenta `!` rather
than being clamped. These are raw readings, not millimeters, calibrated travel
or a pressed/released indication; no autoscaling is performed.

Columns retain ASIC sensor-index order. Labels come directly from the same
production-derived grid/base-action tables used by firmware, including the
ANSI/ISO boot-time FN/right-Alt patch. They identify the base key, not its
current FN-layer action. Every displayed label is one ASCII character followed
by one space; each block below uses the same two-character cell. Letters,
digits and punctuation keep their usual labels. Special labels are `e` Escape,
`t` Tab, `b` Backspace, `c` Caps, `r` Enter, `_` Space, `m` Menu, `f` FN,
`^` Ctrl, `s` Shift, `a` Alt and `g` GUI. Left/right modifiers share a label;
their sensor positions distinguish them. International usages use `i` without
guessing localized keycap legends; the extra ISO backslash uses `\`.

Use a UTF-8, ANSI-compatible terminal. Each key/bar occupies two columns;
the full ANSI/ISO/JIS rows require 139/141/147 terminal columns respectively,
including the range/scale prefix and a spare column to avoid wrapping.
On narrower terminals only the fitting sensor window is shown; the caption
explicitly reports its zero-based inclusive range and total sensor count.
`--start` changes its first index, and resizing adjusts its length. Cursor
movement/line clearing prevent scrolling; wrapping is disabled during the
display and restored, together with color/cursor visibility and input tty
attributes, on normal exit or Ctrl-C. No firmware change or flash is needed.

The original three-column display validation used a 200-column pseudo-terminal attached to the real CDC
stream: 4,835 records received, 60 updates, 4,775 not displayed in three
seconds. Replaying the cursor/erase sequences produced exactly two content
rows with 61 aligned key/bar columns and terminal cleanup sequences. USB
device number stayed 11. Host tests cover all three label maps, raw scale
boundaries/invalid values, viewport resizing, repeated in-place redraws,
layout changes and cleanup. The source tables were rechecked against the
hash-pinned production firmware through its scatter initialization.
The compact display tests additionally verify every label and bar occupies
exactly two characters, with a trailing space, across all three layouts.
