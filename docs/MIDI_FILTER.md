# MIDI mapping, velocity pop filter and octave indication

The filter, revised map and octave indicators are included in the complete
`keyboard-fn-menu` application.
No device access or reset is needed for the offline checks here.

## Mapping

The root README contains the exact 43-key map requested by the user. The upper
row spans Tab=C5 (72) through backslash=B6 (95); the lower row spans Left
Shift=C4 (60) through apostrophe=F#5 (78). These ranges intentionally overlap:
for example, Tab and M both send C5. Existing duplicate-note ownership rules
apply: first down starts the note, final release ends it, and aftertouch uses
the maximum travel of the held keys mapped to that note.

Left Shift is identified from its recovered modifier action, not as a printable
HID usage. It remains Shift in keyboard mode and becomes a configurable C4 note
in MIDI mode. Fn is a menu control; Right Alt/Ctrl shift octave, Left Ctrl/Alt
bend pitch down/up, and Left Windows supplies modulation. These controls
cannot be remapped to notes; wheel values do not use the velocity filter.
The GUI displays sharp note names and still accepts flat spellings as input.
It reads mapping state from the device. Importing a host profile replaces
defaults with that file's stored mappings.

## Four-interval estimator

Take five actual post-trigger ADC readbacks y1…y5.
The triggering sample is excluded. Form four signed differences:

```
d0 = y1 - y2
d1 = y2 - y3
d2 = y3 - y4
d3 = y4 - y5
```

Decreasing ADC values indicate increasing press depth, so positive differences
mean positive press velocity. All intervals assume a 1/8000-second duration.
Because their durations are equal, filtering raw differences is equivalent to
filtering their counts/second rates.

Sort the four differences, define their median as the midpoint of the two
middle values, and discard exactly one interval with the largest absolute
distance from that median. In an exact tie, discard the earliest interval in
sample order. This deterministic rule applies even when no strong outlier
exists; it does not introduce an extra noise threshold or discard two values.

Average the remaining three differences, multiply by 8000, then clamp/normalize
to 0…1 using the existing maximum of 4,500,000 counts/s. Fractions are preserved;
there is no integer division before normalization. The MCU supplies this float
to the GUI and rounds it to MIDI attack velocity 1…127. Signed nonpositive
estimates normalize to zero, but Note On still uses at least velocity 1 because
zero-velocity Note On means Note Off. Aftertouch calculation is unchanged.

Examples (intervals are signed ADC-count differences):

| Four intervals | Discard | Mean of other three | Counts/s |
| --- | ---: | ---: | ---: |
| 100, 1000, 100, 100 | 1000 | 100 | 800000 |
| 10, −1000, 10, 10 | −1000 | 10 | 80000 |
| 1, 1, 2, 20 | 20 | 4/3 | 10666.666… |
| 0, 10, 20, 30 | first interval, 0 | 20 | 160000 |
| 30, 20, 10, 0 | first interval, 30 | 10 | 80000 |

The last two examples expose the explicit tie policy rather than claiming
there is a uniquely identifiable outlier in a symmetric set.

This is an **interval-outlier** filter. One corrupted interior ADC sample can
perturb two adjacent intervals; removing exactly one interval cannot guarantee
repair of arbitrary sample spikes. Invalid samples outside the accepted ADC
range still invalidate the raw frame and cancel pending strikes. No extra
latency, FIFO, scan skipping or sharing of velocity histories was introduced.
Each key's overlapping pending windows and release-threshold rearming remain
independent. The existing 8 kHz assumption is not a measured scan-rate claim.

## Host capture consistency

`decode_scan_stream.py --last-key` uses the same interval filter in the host
capture helper. It prints signed raw counts/s to three decimals, rather than
normalizing or rounding away the fractional mean. All twenty captured readbacks
are still printed; filtering changes the velocity estimate, not the data stream.
The GUI continues to display the float received from firmware without host-side
filtering. Telemetry uses HKG6.

## Octave LEDs

Only the control matching the shift direction blinks: Right Alt for negative,
Right Ctrl for positive. It alternates blue (PWM 0,0,255 before global brightness)
and off with equal duty cycle. The
full period is `120 * (11 - abs(octave))` ms, for offsets limited to ±10. Thus
each additional octave strictly increases blink speed. At zero, neither has
an octave overlay and all five octave/wheel controls remain steady blue;
in keyboard mode the stored offset does not flash either key and all five use
inverse-travel lighting.
The persistent Enter mode marker remains; the held Fn+Enter mode-name display
takes priority. Existing lighting-off, invalid-scan and stale-frame blanking apply.
All channels come from the recovered per-profile LED map.

## Build and verification

```
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset keyboard-fn-menu
cmake --build --preset keyboard-fn-menu
# Requires the separate read-only production reference and audit dependencies:
cmake --build --preset keyboard-fn-menu --target audit-keyboard
```

Tests cover all 43 defaults, Left Shift's two roles, an actual USB-MIDI Note On
with filtered attack velocity, high/low interval outliers at every position,
deterministic ties, fractional means and normalization limits. Full-history
randomized tests retain 16640 per-key scan frames and validate concurrent and
overlapping window completion. Octave LED tests cover both signs and all ten
magnitudes across ANSI, ISO and JIS, both phases and zero/keyboard mode. Text
tests cover mode-name priority and release cancellation. Hardware visibility
and filtered playing response are player checks,
not conclusions from these models. See [hardware validation limits](CALIBRATION.md#validation-status).
