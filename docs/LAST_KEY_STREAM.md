# Triggered 20-sample key capture

Status: flashed once with explicit authorization on 2026-09-05; compact CDC
and the actual host CLI passed the hardware checks below. The earlier lighting
image is preserved in `build-lighting`; build this revision with `last-key`.

## Usage

After flashing the new application, with scanning running and no other CDC
reader, open the device directly:

```sh
python3 -u tools/decode_scan_stream.py --last-key --threshold 3600
python3 -u tools/decode_scan_stream.py --last-key --threshold 3600 --repeat
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3700
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key > values.txt
```

With `--last-key`, omitting the device defaults to `/dev/ttyACM0`, not the
interactive terminal. Specify a different device path if needed. To replay
HKL1 from a pipe or redirected stdin, explicitly supply `-`; interactive tty
stdin is rejected so a mode command cannot accidentally be sent to the console.
Other display modes retain their existing stdin default. This default-device
fix is host-only and does not require another firmware flash.

The user running this command needs read/write access to the device. The host
sets the tty raw and sends `stream key THRESHOLD SESSION`, where SESSION is a
random uint32 nonce. Matching START/sequence-zero metadata acknowledges the
new capture and prevents old queued data being mistaken for its samples.
The tool does not flush tty input, reset the keyboard, start/restart the ASIC,
enable host keystrokes, or change lighting. This firmware preset starts scanning
and lighting automatically after USB configuration, as the lighting preset did.

Press means raw **strictly less than** the threshold (default 3800, range
1..4096). A new downward crossing selects that sensor. A sensor already below
the threshold on the session's first scan counts as pressed. If multiple
sensors cross in the same report, the lowest raw sensor index wins: sub-scan
ordering is unknown. Release is raw greater than or equal to the threshold.
There is no hysteresis or debounce in this selection rule.

The host prints and immediately flushes `Capture Armed` with the input path,
strict lower-than threshold, layout, sample count and report timeout. This
means it is waiting for the stream/trigger, not that the device has already
acknowledged the command. When the first sensor is selected, it prints e.g.
`Key: A (sensor 32)`, then **exactly 20 subsequent decimal readbacks**, one per
line, followed by a velocity estimate, and exits successfully. The trigger sample itself is excluded. Readings
include unchanged values and release; there is no rate limiting, interpolation,
latest-only replacement or skipped report within the requested interval.

Velocity uses a least-squares straight-line fit to the **first five printed
readbacks**, with fixed 125 microsecond spacing as requested (assumed 8000 Hz).
Those five points span 0.5 ms under that assumption. For samples `y0..y4`,
the estimate is `800 * (2*y0 + y1 - y3 - 2*y4)` **raw counts/second**.
The raw slope is negated so positive means pressing/decreasing raw values;
negative means releasing, and a flat fit gives zero. The trigger sample and
remaining fifteen readings do not influence the estimate. The result is
printed after all twenty readings, and only for a complete valid capture.
The startup banner and result explicitly identify the 8 kHz assumption.
This is not calibrated millimeters/second and does not use measured delivery
timing: the actual observed hardware rate remains lower than 8 kHz.

### Repeat captures

Add `--repeat` to keep the same stream/session open after each capture. Each
cycle prints the triggering key, its next 20 values and the five-point velocity.
The host then consumes and validates every report without printing the held
key's additional values. When **that captured key** reads strictly greater
than the threshold, it prints `Capture Armed` with the release value and
trigger threshold, resets the capture/velocity window, and waits for another
below-threshold press. Equality does not re-arm or trigger. If the twentieth
sample is already above threshold, it re-arms immediately after printing that
capture's velocity. A release earlier inside the fixed twenty-sample window
does not interrupt the capture or start overlapping captures.

After re-arming, continued released values do not retrigger; either the same
key or a newly selected key can trigger the next capture. During a capture
or while waiting for release, a selected-sensor change prints a `WARNING`
and `Capture Armed` restart message. The host discards the incomplete capture's
sample count and velocity window; already printed partial values remain in
the output but are explicitly marked as discarded by the warning. If the
previous capture was complete, the warning instead notes that its release
was not observed; its completed result remains valid. The new sensor's current
below-threshold value becomes a fresh trigger (excluded from its next 20
readbacks). If it is not below threshold, the host waits until it is. No
device mode restart or reflash is needed.

The process stays alive through completed captures and idle periods until
Ctrl-C (exit 130). Existing safety failures—data loss, overflow, corruption,
device disconnect, output stall or report timeout—still exit nonzero. EOF in
repeat replay is also an error, not a successful end of the ongoing capture.
Sequence/checksum validation continues through the unprinted held/released
reports; no intermediate reports are dropped or skipped by the receiver.

Key labels default to the ANSI layout verified on this board. Use `--layout
iso` or `--layout jis` for other physical layouts; HKL1 does not carry layout
metadata. The device's existing compact mode can select another newly pressed
key. The warning-and-restart behavior applies to both one-shot and repeat
captures; samples from different sensors never contribute to the same velocity
fit. Missing/truncated reports still fail. In one-shot mode, reports after the
completed 20-sample interval are outside the capture and are not consumed.
This behavior is host-only and works with the currently flashed firmware.

`--buffer-frames` sets the pending host output limit (default 8192). Output is
nonblocking and may batch consecutive lines into writes without omitting any.
A full output queue fails immediately rather than blocking acquisition.
`--timeout` sets the maximum interval without a complete report (default five
seconds), including mode negotiation; a blocked output also times out. Valid
unselected reports keep the capture armed indefinitely until a trigger.
These options only apply to `--last-key`;
display modes, `--rate`, `--duration`, `--summary` and `--hex` cannot be combined
with it. Ctrl-C ends capture with status 130 (interrupted, not a successful
complete capture); the tty settings are restored on exit.

The keyboard remains in compact mode after the reader exits. Another
`--last-key` invocation starts a fresh capture session. To restore the existing
whole-keyboard HKS1 stream, send `stream on` through a CDC command client.
`stream off` stops either stream without stopping scanning. Do not run two
readers on the CDC device. Files or non-tty stdin can replay a captured HKL1
session; they send no command and must include its START record.

## Rate, buffering and failure behavior

The device performs selection on every accepted full optical scan and transmits
one **20-byte HKL1 report**, instead of the 160-byte HKS1 whole-keyboard report:
eight times less CDC payload. At a hypothetical 8,000 scans/s this is 160 kB/s
instead of 1.28 MB/s, excluding USB overhead. There is no changed SPI clock or
ASIC scheduler, and **this is not a claim of achieving 8 kHz**. The previous
hardware measurements were about 1.6–1.9 kHz with whole-keyboard streaming.

Both formats reuse the existing 5120-byte firmware queue and 640-byte stable
USB transfer buffer. Compact mode holds 256 queued reports and up to 32 in
flight. A compact queue overflow latches a fault, stops accepting samples for
that session, drains the intact queued reports and then sends an OVERFLOW
report—even if no further scans arrive. No implicit restart, overwrite,
GPIO retry or MCU reset is performed. USB cancellation/disconnection also
faults the session; a new explicit command is required to resume it.

The host exits with status 1 and an error on **stderr** for device overflow,
host output overflow, sequence gaps/duplicates, unexpected session changes,
bad framing/checksum, invalid raw values/layout changes, truncation, I/O
failure or report timeout. Once the session starts it never resynchronizes
past damaged bytes. Already-emitted numbers are a prefix of a failed capture,
not proof the entire run was lossless; consumers must check the exit status.
No host can reconstruct reports lost before capture starts or guarantee
detection of every possible corruption with a finite checksum. Loss checking
applies to reports in the acknowledged session, not historical device drops or
unobserved internal ASIC conversions.

## HKL1 wire format

All integers are little-endian. Every report is 20 bytes.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `HKL1` |
| 4 | 4 | Host-selected session nonce; manual command default 0 |
| 8 | 4 | Sequence, starts at 0; wraps modulo 2^32 |
| 12 | 2 | Selected raw value, or 0 before selection |
| 14 | 1 | Raw sensor index 0..64, or 255 before selection |
| 15 | 1 | Flags: bit 0 START, bit 1 OVERFLOW/data loss, bit 2 invalid scan/layout |
| 16 | 2 | Accepted threshold |
| 18 | 2 | Sum of preceding nine uint16 words, modulo 65536 |

## Build and validation

```sh
cmake --preset last-key
cmake --build --preset last-key
ctest --preset host-tests
cmake --build --preset last-key --target audit-keyboard audit-lighting
```

Current host tests cover the startup banner being flushed before any input,
key identification, exactly 20 readings after the trigger, early EOF and
key-switch warning/restarts with fresh velocity windows, and exit without waiting for the input to close. They
also cover five-point velocity sign/scaling, flat/noisy inputs, exclusion of
the trigger and later samples, repeat release boundaries, independent repeated
velocity windows, two complete captures in a still-running process terminated
only by SIGINT, loss detection between captures, fragmentation,
session nonce negotiation over a pseudo-tty, tty restoration, sequence wrap,
corruption/gaps/truncation, report timeout and fatal overflow with stdout
deliberately blocked. Compiled ARM/SDK tests cover threshold crossings,
simultaneous presses, release tracking, every synthetic sample at FS/HS,
mode switches with an immutable old USB packet, device queue overflow and
USB reset fail-stop behavior, and actual CDC command parsing through the
optical scan path. Existing whole-keyboard, USB, FN and lighting regressions
remain part of the audit. None of these tests opens or flashes the keyboard.

Reviewed application: `build-last-key/huntsman_firmware.bin`, exactly 131072
bytes at `0x20000000`, SHA-256
`9050d97a7096fb549d8aaa7f1d99790f9701e692b29301ca320d93cb276bdfa8`.
Load size 52656 bytes; SRAMX including heap 18696/24576 bytes; USB SRAM
15488/16384 bytes. The bootloader region is outside this image.

## Authorized flash and hardware validation (2026-09-05, earlier continuous host mode)

- One software bootloader entry and one supplied-updater application flash;
  all 2048 64-byte program blocks acknowledged. No independent flash readback,
  retries, manual resets or writes to bootloader/secondary/factory regions.
- USB port `3-2.1`: application device 13, bootloader 14, new application 15.
  Returned at 480 Mbit/s with keyboard, MIDI and CDC drivers and the same
  updater serial response. No further USB disconnect during validation.
- Threshold 3800, nonce-selected compact session: 4942 reports in approximately
  three seconds, contiguous sequence numbers and no overflow, invalid-sample
  or checksum errors. Measured delivery about **1647 reports/s, not 8 kHz**.
  Selected raw range 3783..3792; a below-threshold sensor was already present,
  so this does not demonstrate a human press or release.
- Ran the actual `decode_scan_stream.py /dev/ttyACM0 --last-key --threshold
  4096` process with stdout continuously drained. It produced 4975 decimal-only
  LF-terminated lines, raw range 3928..3937, empty stderr, then exited 130 on
  intentional SIGINT. The permissive threshold exercised selection/output
  without requiring a physical key press.
- Scanner stayed settled/valid with 61 sensors and errors 0. Lighting stayed
  running with transfers/frames increasing and errors 0. Calibration remains
  0; host keystrokes remain disabled. Kernel warnings were the existing MIDI
  1.0 fallback, control-only HID endpoint and usbfs claim messages.
- Restored whole-keyboard HKS1 streaming after testing. Opening `--last-key`
  requests a new compact session automatically. No test process remains open.

Device/host overflow failure paths were tested offline, not deliberately
provoked on the physical keyboard. Compact payload reduction is verified;
the underlying acquisition-rate limit remains unresolved.
