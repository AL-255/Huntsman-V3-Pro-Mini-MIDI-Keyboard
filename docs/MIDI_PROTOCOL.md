# MIDI and GUI protocol

## USB-MIDI 1.0

The existing composite descriptors are unchanged: Audio Control interface 1,
MIDI Streaming interface 2, bulk OUT `0x02`, bulk IN `0x82`. The firmware emits
four-byte USB-MIDI event packets on cable 0, MIDI channel 1:

| Event | Byte 0 (cable/CIN) | Byte 1 | Byte 2 | Byte 3 |
| --- | --- | --- | --- | --- |
| Note On | `09` | `90` | note 0…127 | velocity 1…127 |
| Note Off | `08` | `80` | latched note | 0 |
| Poly Key Pressure | `0A` | `A0` | sounding note | pressure 0…127 |
| Cleanup All Sound Off | `0B` | `B0` | 120 | 0 |
| Cleanup All Notes Off | `0B` | `B0` | 123 | 0 |

This is MIDI 1.0, not MIDI 2.0 UMP, MPE, channel pressure or raw UART MIDI.
Note names are a GUI convention: C0=12, middle C/C4=60. Flat spellings are
accepted and displayed where appropriate. Inbound MIDI packets are received
and the existing OUT endpoint is rearmed, but they do not control this
application's synth, mapping or lights. There is no built-in synthesizer.

## CDC commands

Use raw serial I/O and serialize commands, because snapshots acknowledge only
the latest command ID. IDs must be nonzero decimal uint32 values. Commands
are newline-delimited ASCII and do not require a meaningful UART baud rate.

```
stream gui
cfg get ID
cfg set ID SENSOR PRESS RELEASE
cfg all ID PRESS RELEASE
cfg enable ID 0_OR_1
cfg midi ID SENSOR NOTE
```

`cfg midi` accepts 0…127 or 255 (unmapped). It rejects unsupported sensor
indices and Fn/left Ctrl/left Alt controls. The current identified profile
must exist. It releases active output, cancels pending strikes, increments the
shared RAM configuration revision and changes one mapping. On success, the
snapshot contains the exact new value; the GUI checks both ACK and readback.

`cfg enable` governs HID and MIDI performance output, not raw scanning or the
velocity monitor. Threshold edits and all-key application retain their existing
Schmitt validation and neutral-arming rules. These commands never write flash,
enter the bootloader or reset the MCU. There is no device-save command.

The GUI allows one outstanding command, with a 3 s ACK timeout and no automatic
retry. Rejection, mismatched readback, malformed telemetry or stale/disconnected
CDC stops the worker and cancels unsent queued changes. Bulk profile import is
not atomic across all commands: already acknowledged changes remain if a later
command fails, and output may remain disabled. Reconnect and inspect the device
before deciding whether to apply again.

## HKG4 telemetry

1152-byte, little-endian, latest-only snapshots, no faster than one per 33 ms.
HKG4 extends the HKG3 layout without moving the sensor or velocity arrays:

| Offset | Encoding | Meaning |
| ---: | --- | --- |
| 0 | 4 bytes | `HKG4` |
| 4 | u16 | 1152 |
| 6 | u8 | version 4 |
| 7, 8 | u8 each | profile 0…3, count 0/61/62/65 |
| 9 | u8 flags | enabled=1, armed=2, valid=4, scan fault=8, LED fault=16, Fn held=32 |
| 10 | u8 | last result: initial=0, success=1, rejected=2 |
| 11 | u8 | legacy Fn editor mode 0…2, **not** performance mode |
| 12 | u32 | GUI sequence |
| 16 | u32 | RAM config revision |
| 20 | u32 | last command ID |
| 24, 28 | u32 each | optical/LED error counts |
| 32 | 65 × u16 | raw samples |
| 162 | 65 × u16 | press thresholds |
| 292 | 65 × u16 | release thresholds |
| 422 | 9 bytes | sensor-down bitset |
| 431 | 16 bytes | last accepted NKRO USB report |
| 447 | 65 × float32 | normalized device velocity, 0…1 |
| 707 | 65 × u32 | completed velocity fit counts |
| 967 | 65 × u8 | velocity state: ready=1, valid=2, pending=4 |
| 1032 | u8 | performance mode: keyboard=0, MIDI=1 |
| 1033 | i8 | octave offset −10…+10 |
| 1034 | u8 | MIDI channel, currently always 1 |
| 1035 | u8 | MIDI cleanup pending, 0 or 1 |
| 1036 | 65 × u8 | base note per sensor; 255=unmapped |
| 1101 | 3 bytes | zero padding |
| 1104 | u32 | MIDI queue-overflow count |
| 1108 | u32 | performance-mode change count |
| 1112 | 36 bytes | zero padding |
| 1148 | u32 | sum of the preceding 574 little-endian u16 words |

Unused sensor slots are zero, including MIDI mapping padding; **active** unmapped
sensor slots are 255. This checksum detects framing errors, not authentication.
The decoder validates size/version pairs, reserved bytes, value ranges and
padding. The GUI reads legacy HKG1/480-byte and HKG2/HKG3/1088-byte snapshots as
well; MIDI controls stay disabled on those older firmware versions. Old GUIs
that know only HKG3 need updating before connecting to HKG4 firmware.

HKG4 velocity is float32 as in HKG3. HKG2 alone used signed integer counts/s;
the host does not reinterpret old integers as normalized floats. Pressure is
transmitted over MIDI, not duplicated as another HKG4 sensor array.

## Host JSON

Version 1 remains threshold-only. Version 2 adds integer `midi` to every one of
the 61 ANSI key objects:

```json
{"sensor": 32, "label": "A", "press": 3600, "release": 3700, "midi": 60}
```

The surrounding object has `version: 2`, `layout: "ansi"`, and `keys` containing
all 61 unique, correctly labelled sensors. Notes are 0…127 or 255; reserved
control keys must use 255. Invalid pairs, boolean numeric fields, duplicates,
wrong labels, missing entries and invalid MIDI values are rejected before
commands are queued. A version-2 file requires HKG4 firmware. Importing a
version-1 file leaves MIDI mappings unchanged. Mode and octave are transient
performance state and are not imported/exported.
