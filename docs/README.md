# Documentation guide

Start with the [project README](../README.md) for operation and the
[build guide](BUILDING.md) for a clean checkout. The current complete preset is
`keyboard-calibration-parallel`, emitting HKG6 telemetry.

## Current behavior and protocols

- [Keyboard GUI](KEYBOARD_GUI.md): layout, Schmitt thresholds, MIDI mappings,
  profiles, calibration controls and connection handling.
- [Per-key velocity](KEY_VELOCITY.md), [normalization](NORMALIZED_VELOCITY.md)
  and [interval pop filter](MIDI_FILTER.md): independent capture and encoding.
- [MIDI design](MIDI_DESIGN.md) and [USB/GUI protocol](MIDI_PROTOCOL.md).
- [Parallel calibration](CALIBRATION.md): operation, state machine, HKG6 fields
  and the completed physical calibration/readback record.
- [Device storage](DEVICE_CONFIG_STORAGE.md): two-page ownership, record layout,
  validation, recovery limits and serial-number protection.
- [Flash acquisition](FLASH_DUMP.md): read-only dump commands and private backups.
- [Whole scan display](SCAN_STREAM.md), [20-sample capture](LAST_KEY_STREAM.md)
  and [travel lighting](TRAVEL_LIGHTING.md): operation and protocol details.
- [USB integration](USB_DESIGN.md) and [optical/Fn design](KEYBOARD_RECOVERY.md).
- [SDK provenance and licenses](../third_party/ORIGINS.md).

Project-authored documentation describes only the latest build. Update or
remove obsolete claims in place; do not append development snapshots. Keep
unverified behavior explicit. This is enforced as a contributor rule in
[AGENTS.md](../AGENTS.md). Vendored SDK documentation retains its upstream content.

## Current evidence boundary

The installed 128 KiB image has SHA256
`6639660b99e8182183304c3939563d14c4243bedfbead3986826cd164aea3084`.
Its full flash readback matched twice. A subsequent user-operated calibration
completed all 61 keys; generation 1 was independently retrieved from 0x7d400
with valid CRC and two matching reads. See [the detailed record](CALIBRATION.md).
Physical power-cut recovery, cold-boot reload of that saved record, calibrated
force/distance, exact 8 kHz acquisition and comprehensive USB/DAW compliance
are not claimed. Tests and documentation checks do not flash hardware.
