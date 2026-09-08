# Documentation guide

Start with the illustrated [user manual](../USER_MANUAL.md) for everyday operation,
the [project README](../README.md) for a project overview, and the
[build guide](BUILDING.md) for a clean checkout. The current complete preset is
`huntsman` (alias `keyboard-fn-menu`), emitting HKG6 telemetry.

## Current behavior and protocols

- [Fn menu](FN_MENU.md): supported hints, trigger-point editor and brightness.
- [Keyboard GUI](KEYBOARD_GUI.md): layout, Schmitt thresholds, MIDI mappings,
  profiles, calibration controls and connection handling.
- [Per-key velocity](KEY_VELOCITY.md), [normalization](NORMALIZED_VELOCITY.md)
  and [interval pop filter](MIDI_FILTER.md): independent capture and encoding.
- [MIDI design](MIDI_DESIGN.md) and [USB/GUI protocol](MIDI_PROTOCOL.md).
- [Root/scale selection](MIDI_SCALES.md): portable interval tables, selectors,
  shared note/LED filtering, modal safety and RAM-only state.
- [Parallel calibration](CALIBRATION.md): operation, state machine, HKG6 fields
  and validation limits.
- [Device storage](DEVICE_CONFIG_STORAGE.md): two-page ownership, record layout,
  validation, recovery limits and serial-number protection.
- [Flash acquisition](FLASH_DUMP.md): read-only dump commands and private backups.
- [Whole scan display](SCAN_STREAM.md), [20-sample capture](LAST_KEY_STREAM.md)
  and [travel lighting](TRAVEL_LIGHTING.md): operation and protocol details.
- [USB integration](USB_DESIGN.md) and [optical/Fn design](KEYBOARD_RECOVERY.md).
- [SDK provenance and licenses](../third_party/ORIGINS.md).

## Application and board ports

- [Architecture](ARCHITECTURE.md): shared application, board contracts and compatibility.
- [Porting guide](PORTING.md): build selection, samples, keys, LEDs, USB and storage.
- [Scheduling](SCHEDULING.md): cooperative ownership and the FreeRTOS tradeoff.

Project-authored documentation describes only the latest build. Update or
remove obsolete claims in place; do not append development snapshots. Keep
unverified behavior explicit. This is enforced as a contributor rule in
[AGENTS.md](../AGENTS.md). Vendored SDK documentation retains its upstream content.

## Current evidence boundary

The latest build is validated by native tests and compiled ARM/register models,
including comparison with original editor instructions and number-row colors.
The current refactored application has not been flashed or physically validated.
See [validation limits](CALIBRATION.md#validation-status).
Tests do not establish physical LED appearance, 8 kHz acquisition, calibrated
force/distance or comprehensive USB/DAW compliance, and do not flash hardware.
