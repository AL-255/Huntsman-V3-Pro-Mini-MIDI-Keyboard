# Whole-keyboard CDC scan stream

This revision is committed for review/build, **not flashed**. The running
keyboard retains the previously validated diagnostic image. The periodic
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

**8 kHz fresh hardware sampling has not been established.** Production SPI is
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
unsigned decimal; with it, four-digit hex. The decoder accepts arbitrary
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
