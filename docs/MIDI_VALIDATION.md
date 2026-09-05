# MIDI candidate validation — 2026-09-05

## Exact artifact

Preset/directory: `keyboard-midi` / `build-keyboard-midi`.
Binary: `huntsman_firmware.bin`, 131072 bytes.
SHA-256: `fb613c37167d6326fcf8597af49d1945549f17a8e5fc40af856a230918a89164`.
Target compiler: Arm GNU 14.2.1, Release, official vendored NXP SDK libraries.

Link use: 59388 bytes application load, 23816 bytes SRAMX, 15488 bytes USB SRAM,
separate 8192-byte stack, 1024-byte reserved configuration image area. No physical
configuration flash writer is present. The stock persistent areas were not used.

An isolated export of the staged source tree, without the sibling extraction
or pre-existing build artifacts, passed all seven host suites and built a
**byte-identical** application binary. Thus the documented standard build does
not depend on the private extraction or the original installer EXE. Optional
reference-backed audits still need the separately supplied primary application.

## Offline checks completed before flash

- Seven CTest suites: core, keyboard raw, MIDI, GUI/PTY, image reservation,
  scan display and compact last-key stream.
- MIDI native tests: exact 24 defaults, role protection, mode-chord hold,
  five-sample velocity, aftertouch, short/overlapping taps, duplicate pitches,
  octave-latched release, muting out of range, endpoint backpressure,
  fail-safe FIFO overflow and many independent voices across all layouts.
- Linked ARM `audit-keyboard`: full/high-speed USB descriptor/control and
  endpoint paths, CDC/parser, NKRO, MIDI packets, updater reset deferral,
  startup/alignment/chirp checks; optical DMA → MIDI velocity/pressure,
  GUI mapping ACK/rejections, reset cleanup and HID isolation/recovery;
  existing raw velocity/Schmitt and scan-stream regressions.
- Linked ARM lighting tests for ANSI/ISO/JIS: production-derived controller
  initialization and mappings, travel PWM with mode marker, immutable pending
  transfers, maintenance, off/on/invalid/stale behavior, I2C NACK/stall isolation.
- Actual Tk under a private Xvfb display with a simulated CDC peer: geometry,
  selection, apply-one/apply-all thresholds, MIDI mapping/readback and reserved
  controls, enable/disable, disconnect, demo and resizing.
- Image validator and whitespace checks; fresh binary matches the reviewed
  flashed artifact. No firmware code changed after that flash.

## One authorized hardware update

The user explicitly requested `flash it now`. The supplied sibling updater's
`enter_bootloader()` and `flash_app_image()` were used once after checking image
size/hash, updater serial `OPENHUNTSMAN0001`, unique target and USB port `3-2.1`.

Kernel times: application device 19 disconnected at 04:45:11, bootloader device
20 appeared, and application device 21 returned at 04:45:27. All 2048 64-byte
program chunks were acknowledged. This is transport/program-ACK evidence, not
an independent flash readback. There was no retry, unplug, forced boot mode,
bootloader write, secondary-controller update or stock-configuration write.

Post-update USB speed was 480 Mbit/s, VID:PID `1532:02b0`. Interfaces 0/1–2/4–5
bound to `usbhid` / `snd-usb-audio` / `cdc_acm`. Updater interface 3 remained
unbound as expected for the existing control-only HID transport. The updater
serial query still succeeded. ALSA listed the MIDI endpoint as `hw:3,0,0` on
this machine; card numbers are not stable identifiers for other systems.

Kernel messages contained the expected boot/application enumeration cycle,
MIDI 1.0 fallback and the known control-only HID/no-interrupt-endpoint and
usbfs-interface-not-claimed warnings. These are not new endpoint failures.

## Post-flash CDC health check

The user's GUI initially owned `/dev/ttyACM0`; it was not terminated or bypassed.
After that port was released, the current GUI transport was used for a 12-second
check, with only `stream gui` and an acknowledged `cfg get` query:

- 365 distinct HKG4 snapshots, sequences 2107…2471.
- Profile ANSI/61, flags 7: output enabled, armed and scan valid.
- All threshold pairs 3600/3700 and all 24 specified MIDI defaults verified.
- Final raw range 3784…4016, no down sensors observed, keyboard mode throughout.
- Scan errors 0, LED errors 0, MIDI queue-overflow errors 0.
- No mapping, threshold, enable or performance-mode changes were sent.
- CDC closed cleanly after the test; GUI reconnection is available.

## What remains unverified

There was no physical key press during the CDC check. Actual Fn+Enter behavior,
visible green/blue pulses, playable latency, pressure response and received
MIDI in a DAW still require a player check. The ARM tests establish software
encoding under modeled input, not physical force calibration or a measured
8000 Hz scan cadence. Device-side configuration persistence remains unimplemented.

A suggested non-destructive player check is to route channel 1 to a poly-pressure
capable instrument, use Fn+Enter, play/release Tab and Q together, vary key travel,
change octave while holding a note and release it, then return to keyboard mode.
No further flashing, forced bootloader entry or cable cycling is needed for
that check.
