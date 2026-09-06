# Keyboard and MIDI performance design

Current application: `keyboard-calibration-parallel`, installed and read back
on hardware. See
[current validation](CALIBRATION.md#validation-status) and
[filter design](MIDI_FILTER.md).
This is application behavior, not a claim that the stock firmware implements
MIDI. Existing production-derived sensor/LED maps and board initialization remain
the hardware reference. MIDI did not add a peripheral reset sequence.
Calibration uses a separately bounded two-page flash writer.

## Ownership and scan flow

`keyboard_raw.c` owns per-sensor Schmitt state and independent five-sample
velocity registration. `keyboard_midi.c` owns performance mode, MIDI mapping,
octave, pending strikes, note ownership and MIDI transmission scheduling.
`keyboard_live.c` joins these to the existing optical, USB and lighting services.
The USB stack continues to own a stable four-byte MIDI IN transfer buffer.

```
valid optical frame → raw Schmitt edges and per-key velocity windows
                   → keyboard-mode Fn+C / active calibration (output suppressed)
                   → otherwise priority Fn+Enter mode chord
                   → keyboard: existing NKRO/Fn engine
                   → MIDI: delayed strikes → ordered Note On/Off queue
                           held-note travel → latest poly-pressure values
main loop → queued note events first → changed pressure values → NXP USB IN
```

The MIDI state is 1516 bytes, with fixed capacities and no dynamic allocation.
The current application uses 23816/24576 bytes SRAMX,
15488/16384 bytes USB SRAM, and a separate 8192-byte stack. The reserved final
1024 application-image bytes remain unused. Calibration state uses 1324 bytes
of writable application-image RAM, separate from velocity/MIDI state. The
firmware binary remains 128 KiB.

## Mode and key routing

Power-on mode is keyboard. Each fully processed frame checks whether Fn and
Enter are both down. The chord has priority over output dispatch, including
when both cross their thresholds in the same scan. Either press order works.
On a switch, the application clears HID output, cancels MIDI strikes, schedules
note cleanup and invalidates raw arming. All keys must be above their individual
release thresholds before arming again; a held chord cannot toggle repeatedly.

Keyboard mode uses the recovered base/Fn action maps. MIDI mode does not invoke
that HID/configuration engine for raw edges, so performance keys do not type
letters or activate the legacy Fn+Tab/Caps editor. The GUI uses a separate
performance-mode field, distinct from its Fn editor-mode field.

Fn, left Ctrl and left Alt are reserved MIDI controls. Left Ctrl/Alt decrement
or increment a signed octave offset once per down edge, limited to −10…+10.
Simultaneous opposite edges cancel. The offset survives mode switches but resets
on reboot. Transposition is applied when a strike starts. A held or pending
strike keeps its latched note even if the octave changes later. Out-of-range
transposed notes are silent, not wrapped or clamped to another pitch.

Mappings are per raw sensor for the identified ANSI/ISO/JIS layout. Defaults
are assigned using the recovered base HID action, not guessed scan order.
All unmapped keys use sentinel 255; notes are 0…127. The current default has
43 mapped keys: Tab through backslash span C5–B6, and Left Shift through the
apostrophe key span C4–F#5. Left Shift is a note only in MIDI mode. The GUI supports ANSI
geometry; firmware behavior and native polyphony tests cover all three layouts.

## Velocity and short strikes

For samples y1…y5 **after** the press threshold crossing:

```
d = [y1-y2, y2-y3, y3-y4, y4-y5]
outlier = earliest interval with largest abs(d[i] - median(d))
counts_per_second = (sum(d) - d[outlier]) / 3 * 8000
normalized = clamp(counts_per_second / 4500000, 0, 1)
MIDI attack velocity = max(1, round(normalized * 127))
```

For four values, median means the midpoint of the two middle sorted values.
Exactly one interval is discarded, even when all deviations tie. Fractions
are retained until float normalization. The filter adds no scan delay beyond
the five-sample capture window. See [filter edge cases](MIDI_FILTER.md).

The MCU computes both the normalized float and the final MIDI byte. The GUI
does not normalize velocity. A Note On with velocity zero has Note Off semantics,
so the smallest strike is encoded as velocity 1. Release velocity is fixed at
zero; no release-slope measurement is claimed.

Each sensor has five pending-note slots indexed by a shared modulo-five scan
phase, matching the raw engine's five-frame completion pipeline. A strike latches
its transposed note into that phase slot. Five actual scan frames later, the
matching raw velocity result completes and the MIDI Note On is queued. This is
0.625 ms only if the hardware actually returns 8000 frames/s.

A per-key pointer and release-bit mask retain releases that occur before the
velocity fit completes. Such a short tap emits an ordered Note On followed by
Note Off at completion; it is not silently discarded. Its synthesized sound may
be very short or inaudible. A release and repress before the older fit completes
use separate phase slots. Fits never borrow samples or velocities from another
sensor. Invalid samples/config edits cancel all unfinished strikes.

## Polyphonic aftertouch

The message is **Polyphonic Key Pressure**, status `0xA0` on channel 1, not
channel pressure (`0xD0`). Every sounding pitch has its own pressure value.
Pressure increases with normalized optical travel, using the same lower/upper
endpoints and clamping as white travel lighting, then converting to 0…127.
This is a travel proxy, not an additional pressure sensor or calibrated force.
Saved user calibration overrides the recovered/fallback endpoints after scan
settling. See [calibration limits](CALIBRATION.md#measurement-choices).

Pressure is recomputed from the latest hardware frame. Changed values are
scheduled in fair note-number sweeps, at most one sweep start per 10 ms. A busy
endpoint retains its current immutable USB packet; unsent pressure is replaced
by newer values. Under bus load the update cadence can fall below 100 Hz. There
is deliberately no FIFO of old pressure samples. Note edges take priority.

When multiple keys map to the same pitch, a reference count merges their held
voices: the first key produces Note On, the last release produces Note Off,
and pressure is the maximum of their current values. A second held key on the
same pitch does not send a second Note On or steal the first key's velocity.
This prevents an early release from silencing another held key. Different
pitches remain independent; this is not MPE and does not allocate channels.

## Backpressure, cleanup and faults

The 128-entry, three-byte event FIFO contains only Note On/Off events. USB
acceptance removes one event; rejection/busy leaves it queued. The vendor USB
wrapper owns the copied four-byte USB-MIDI event until completion. Pressure is
only serviced after the ordered queue is empty.

If that finite queue fills, the firmware increments the reported MIDI error
counter, stops the current performance state, cancels pending events/strikes,
invalidates raw arming, and enters cleanup. This is an explicit fail-safe, not
an unlimited lossless guarantee. Do not ignore a nonzero MIDI error count.

Mode changes, mapping edits, threshold/enable invalidation, scan faults/staleness
and USB resets also clear voice state and request cleanup. Cleanup sends Note
Off for all 128 pitches, then CC120 (All Sound Off) and CC123 (All Notes Off) on
channel 1. It is outside the ordinary queue and retries each packet when the
endpoint is busy. No new note events are produced until it finishes. Keys
pressed during cleanup require a fresh release/press edge afterward.

Repeated invalid frames do not restart the cleanup sweep. A host that stops
consuming MIDI must not trap the mode chord or block ordinary HID: the user can
still switch back to keyboard mode. Physical USB disconnection cannot deliver
Note Off to an absent host; cleanup resumes after configuration returns. The
synth/host must also handle device removal. Closing CDC alone does not affect
MIDI or keyboard operation.

## LEDs and configuration persistence

The existing LED framebuffer is overlaid with two 150 ms color pulses separated
by 150 ms intervals: green for keyboard, blue for MIDI. Between indications,
Enter is a dim persistent marker. Other keys keep white travel-proportional PWM.
In MIDI mode with a nonzero octave offset, Left Ctrl (negative) or Left Alt
(positive) instead blinks amber. The on and off intervals are each
`60 * (11 - abs(octave))` milliseconds: the full period decreases from 1200 ms
at magnitude 1 to 120 ms at magnitude 10. The shortest half-period remains
longer than the existing 40 ms LED update period. The other control retains
its travel lighting. Zero shift and keyboard mode disable this octave overlay.
The whole-keyboard mode-change pulse takes priority when active.
The overlay uses recovered per-profile channels, not new GPIO or controller
initialization. Existing `light off`, invalid/stale frame blanking and transfer
ownership still take precedence.

Per-key note edits are acknowledged over CDC and invalidate held output, just
like threshold edits. They are **RAM-only**. Version-2 host JSON includes note
mappings alongside threshold pairs; it does not save transient mode/octave.
Primary stock settings and serial-number data remain untouched. Calibration
endpoints, unlike mappings, persist in two verified unused tail pages. The
calibration overlay takes priority while collecting keys, with independent
amber holds and green completion. See [storage](DEVICE_CONFIG_STORAGE.md).

## Validation boundaries

Native tests cover all defaults, threshold equality, exact velocity conversion,
short/overlapping taps, many independent voices across three layouts, duplicate
notes, transposition, out-of-range muting, reserved controls, busy USB, queue
overflow, mode switching during cleanup and invalidation. Existing raw tests
also cover five-point fit normalization/clamps and independent retriggering.

Linked-ARM tests execute optical DMA, actual USB-MIDI packet submission and
completion, mode toggling/hold suppression, Note On velocity 23 for a known
sample slope, poly pressure, latched Note Off, mapping ACK/rejection, USB-reset
cleanup, HID isolation/recovery and CDC transport at both modeled speeds.
Tk/PTY tests exercise actual GUI actions against a simulated serial peer.

These do not establish physical sampling frequency, scan execution-time margin,
LED appearance, audible behavior or host DAW compatibility. Physical application
readback and calibration/save verification do not establish comprehensive
DAW compatibility.

MIDI semantics reference: the MIDI Association's
[zero-velocity Note On discussion](https://midi.org/community/midi-specifications/zero-velocity-note-on).
