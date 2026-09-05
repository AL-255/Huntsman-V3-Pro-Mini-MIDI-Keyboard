# Keyboard and MIDI performance design

Status: implemented, tested offline and **flashed once with authorization**.
USB enumeration passed; physical MIDI playing and LED appearance remain
unvalidated. See [the validation record](MIDI_VALIDATION.md).
This is new application behavior, not a claim that the stock firmware implements
MIDI. Existing production-derived sensor/LED maps and board initialization remain
the hardware reference. No peripheral reset sequence or flash writer was added.

## Ownership and scan flow

`keyboard_raw.c` owns per-sensor Schmitt state and independent five-sample
velocity registration. `keyboard_midi.c` owns performance mode, MIDI mapping,
octave, pending strikes, note ownership and MIDI transmission scheduling.
`keyboard_live.c` joins these to the existing optical, USB and lighting services.
The USB stack continues to own a stable four-byte MIDI IN transfer buffer.

```
valid optical frame → raw Schmitt edges and per-key velocity windows
                   → priority Fn+Enter mode chord
                   → keyboard: existing NKRO/Fn engine
                   → MIDI: delayed strikes → ordered Note On/Off queue
                           held-note travel → latest poly-pressure values
main loop → queued note events first → changed pressure values → NXP USB IN
```

The MIDI state is 1516 bytes, with fixed capacities and no dynamic allocation.
The candidate fits the original memory partition: 23816/24576 bytes SRAMX,
15488/16384 bytes USB SRAM, and a separate 8192-byte stack. The reserved final
1024 application-image bytes remain unused. The firmware binary remains 128 KiB.

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
performance-mode field; its older editor-mode field retains its meaning.

Fn, left Ctrl and left Alt are reserved MIDI controls. Left Ctrl/Alt decrement
or increment a signed octave offset once per down edge, limited to −10…+10.
Simultaneous opposite edges cancel. The offset survives mode switches but resets
on reboot. Transposition is applied when a strike starts. A held or pending
strike keeps its latched note even if the octave changes later. Out-of-range
transposed notes are silent, not wrapped or clamped to another pitch.

Mappings are per raw sensor for the identified ANSI/ISO/JIS layout. Defaults
are assigned using the recovered base HID action, not guessed scan order.
All unmapped keys use sentinel 255; notes are 0…127. The user's unusual T/Y and
bracket/backslash octave jumps are preserved exactly. The GUI supports ANSI
geometry; firmware behavior and native polyphony tests cover all three layouts.

## Velocity and short strikes

For samples y1…y5 **after** the press threshold crossing:

```
counts_per_second = 800 * (2*y1 + y2 - y4 - 2*y5)
normalized = clamp(counts_per_second / 4500000, 0, 1)
MIDI attack velocity = max(1, round(normalized * 127))
```

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
The recovered endpoint fallback/calibration limitations still apply.

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
The overlay uses recovered per-profile channels, not new GPIO or controller
initialization. Existing `light off`, invalid/stale frame blanking and transfer
ownership still take precedence.

Per-key note edits are acknowledged over CDC and invalidate held output, just
like threshold edits. They are **RAM-only**. Version-2 host JSON includes note
mappings alongside threshold pairs; it does not save transient mode/octave.
Stock persistent state remains untouched. See [storage](DEVICE_CONFIG_STORAGE.md).

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
LED appearance, audible behavior or host DAW compatibility. Existing hardware
checkpoints are not validation of MIDI playing on this candidate.

MIDI semantics reference: the MIDI Association's
[zero-velocity Note On discussion](https://midi.org/community/midi-specifications/zero-velocity-note-on).
