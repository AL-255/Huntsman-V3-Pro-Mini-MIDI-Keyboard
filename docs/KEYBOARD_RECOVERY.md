# Keyboard/FN recovery — 2026-09-05

Status: implemented and tested offline, then **flashed once with explicit
authorization**. High-speed enumeration and isolated editor tests over real
CDC passed. Physical optical scanning remains unvalidated and disabled.
No ASIC command or GPIO initialization was requested. The earlier offline
tests do not establish that the off-chip controller works; live observations
are recorded separately below.

## Production reference and entry points

Only the supplied production decompilation, listing, and raw application were
used as behavioral evidence. `../extracted_firmware` remains read-only. The
old broken sibling implementation was not consulted.

Raw reference: `Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin`, SHA-256
`d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27`.

Addresses below identify `FUN_<address>` in
`../extracted_firmware/analysis/primary_app_decompiled.c`; the listing contains
the corresponding actual instructions. Some functions have noncontiguous bodies.

| Address | Recovered role | Implementation |
| --- | --- | --- |
| `0x2000d5d4`, `0x200142fc` | Key-event routing and action dispatch | `src/keyboard_engine.c` |
| `0x2000b4d8` | Normal/FN action lookup | `src/keyboard_layout.c` |
| `0x200141f8` | FN state; release held actions whose layers differ | `src/keyboard_engine.c` |
| `0x2000f41c` | Control action `0x70` enters actuation editor, `0x71` rapid-trigger editor | `src/keyboard_config.c` |
| `0x200134fc` | Editor key handling, level changes, switching and exit | `src/keyboard_config.c` |
| `0x2001a3bc` | Commit thresholds/profile changes | RAM-only subset in `src/keyboard_config.c` |
| `0x2000cbf0`, `0x2000c1cc` | Layout patch and raw-sensor mapping | `src/keyboard_layout.c` |
| `0x20015dec`, `0x2000e354` | Calibration endpoints and inverse raw-to-level conversion | `src/optical_key.c`, `src/keyboard_scan.c` |
| `0x20015c04`, `0x200164ac`, `0x2001620c`, `0x20015bc0` | Normal thresholds, rapid-trigger exclusions, editor previews | `src/keyboard_scan.c` |
| `0x2001a918` | Ordinary/rapid-trigger hysteresis | `src/optical_key.c` |
| `0x2000ca68`, `0x2000dbac`, `0x200174bc` | GPIO sequence, SPI/DMA setup, ASIC scheduler | `src/optical_bus.c`, `src/optical_transport.c` |

The production key IDs are **not HID usages**: FN=`3b`, Tab=`10`, Caps=`1e`,
Escape=`6e`; number-row 1 through 0 are IDs `02` through `0b`.
The compressed grid starts at runtime `0x20020800`. ANSI/ISO initialization
patches positions 52/53 to FN/right-Alt; ANSI FN is raw sensor 43, Tab 17,
Caps 33, Escape 8. The previous row-order approximation was incorrect.
`tools/production_arm.py` runs the original scatter-loader/decompressor to
recover these initialized tables, rather than treating compressed bytes as C arrays.

## Recovered user interaction

- Holding FN sets the FN layer/configuration latch (`0x04000e42`). It is
  distinct from the actuation/rapid editor mode byte (`0x04000901`).
- FN+Tab enters actuation mode 1; FN+Caps enters rapid-trigger sensitivity
  mode 2. Entry requires a press, live FN, FN-layer action, and unlocked profile.
- Releasing FN **does not exit either editor**.
- Number keys 1–0 select levels 1–10. Releases and unrelated keys are consumed.
- Escape commits a dirty edit and exits. FN+the current editor's shortcut
  also commits/exits. The other shortcut commits then switches editor.
- In rapid mode, Caps without FN toggles rapid-trigger enable without exiting.
- Increment/decrement keys are layout-dependent. ANSI/ISO uses IDs `39/40`
  and `3e/81`; JIS uses `19/28` and `26/27`. IDs `53/59` and `4f/54`
  are also accepted. Bounds remain 1–10; pressing at a bound still marks dirty.

Threshold values come from the production tables at `0x2001b486` and
`0x2001dd7a`. Special keys retain production fixed thresholds; actuation
preview excludes editor-control keys. Normal rapid deltas clamp to at least
8, whereas rapid-editor preview uses the table's unclamped high byte.

## Physical scan candidate

The candidate starts USB first, leaving ASIC GPIO/SPI untouched until CDC
`scan start`. It follows the recovered GPIO 8/26/29/30 sequence, 10 ms then
150 ms waits, active-low ready on P0_19, SPI3 mode 1 at 8 MHz, TX DMA0 channel
9/priority 3 and RX channel 8/priority 2. CTIMER2 supplies nominal 125 us ticks,
below USB and its CTIMER3 PHY workaround in interrupt priority. Accepted-frame
rate is not claimed to be 8 kHz: transactions and processing take real time.

The driver is the unmodified official SDK `drivers/lpc_dma/fsl_dma`, not the
unused `drivers/dma` snapshot for another MCU family. LPC5528's fixed request
mapping does not use LPC55S69's inputmux request-enable feature.

Probe, metadata and table reads precede scanning. The exact five-byte command
table at `0x2001ddd6` distinguishes request from reply (mode 8 uses A4 and
expects C0/A8). Layout comes from A2 metadata byte 7. Six three-byte-per-sensor
tables are retained; modes 6 and 8 provide external lower/upper endpoints.
B6/0 starts A0 sampling; the first two A0 responses are skipped. C0/AC markers
are counted, never decoded as samples. Settling accumulates 128 frames, then
performs the original endpoint rebuild before accepting key events.

The diagnostic safety policy intentionally differs from production recovery:
one GPIO route sequence per boot; no automatic route cycling, DMA/SPI restart,
flash write, or reset on an error. A pending transfer times out after 20 ms;
ready times out after 125 ms. Buffers/descriptors remain allocated after fault.
SDK `DMA_AbortTransfer` is not called because it can spin indefinitely on BUSY.
Requests and channel interrupts are disabled without waiting. USB service
continues, and the host report is repeatedly retried as neutral if necessary.

`keys on` separately requires fresh, valid, settled samples and all keys
released outside an editor. USB reset, CDC closure, stale/invalid samples, scan
stop, or fault disarms host reporting. Physical events remain observable while
host reporting is off; isolated `test` commands never affect physical state.

## CDC commands

Commands are printable ASCII lines terminated by LF or CR. RX is queued in
the USB callback; main parses at most 32 bytes per service pass. Overlong,
non-ASCII/NUL-containing, and overflowing lines are discarded, not truncated
into executable commands. Partial input is cleared on USB reset.

| Command | Effect |
| --- | --- |
| `help`, `status` | Commands / physical scan status |
| `scan start` | One explicit ASIC initialization attempt; host keys stay off |
| `scan status` | Phase, layout, frames, errors, settling/calibration counts |
| `scan sample XX` | Raw-index hex: raw ADC, endpoints, level, pressed state |
| `scan stop` | Quarantine scanner; disarm host keys; no restart |
| `keys on`, `keys off`, `keys status` | Arm/disarm/status of physical NKRO/config state |
| `trace on`, `trace off` | Log physical key ID, edge and level; default off |
| `test reset`, `test profile 1/2/3` | Reset isolated engine / choose layout |
| `test key XX down/up` | Inject production key ID into isolated engine only |
| `test status`, `test report` | Isolated editor state / 16-byte NKRO report in hex |

Example isolated actuation edit: `test key 3b down`, `test key 10 down`,
`test key 10 up`, `test key 3b up`, `test key 0b down`, `test key 6e down`.
Expected: mode 1 survives FN release; Escape returns to mode 0 with saved
actuation 10. No host key report, GPIO operation, or persistent write occurs.
Keep the CDC session open while testing physical host keys: closing it disarms
them. Diagnostics are best-effort bounded logging; excessive trace output can
drop text and is not lossless sample capture.

## Build and offline validation

Install `tools/requirements-audit.txt` into the active Python environment.

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --build --preset host-tests --target audit-keyboard

cmake --preset keyboard-diagnostics
cmake --build --preset keyboard-diagnostics
cmake --build --preset keyboard-diagnostics --target audit-keyboard
```

The firmware target includes the USB startup/transport/chirp regression audit.
Neither target opens a device or performs a flash. Override the read-only
reference path with `-DHUNTSMAN_PRODUCTION_REFERENCE=...`; its hash is checked.

Passed checks:

- Exact generated grid, three FN layers, base layer, and threshold tables;
  every sensor mapping compared to the production mapper.
- 12,078 FN/editor state comparisons against original ARM instructions.
- 20,480 raw-to-level and 22,048 ordinary/rapid hysteresis observations.
- 12,150 per-key normal/editor threshold and 312 calibration comparisons.
- Synthetic raw frames through settling/calibration to ordinary NKRO,
  FN+Tab, FN+Caps, selection, toggling and exit for all three layouts.
- Compiled CDC OUT -> vendor IRQ -> RX queue -> parser -> CDC IN at both
  modeled speeds, including fragmentation, overflow and reset isolation.
- Actual SDK DMA descriptors and DMA0 IRQ callbacks through the scanner,
  physical engine and USB NKRO endpoint with synthetic ASIC replies.
- Held-key release and continued CDC responses for timeout, bad header,
  ready-high, marker-only, invalid sample, USB reset and explicit stop.

Models do not reproduce electrical timing, actual DMA bus arbitration,
NVIC preemption, off-chip reset/power behavior, physical keystrokes, or real
firmware-update recovery. Production differential tests stub LED rendering,
profile flash/CRC operations and selected lookup/notification boundaries;
they do not prove complete equivalence to the original application.

Reviewed local candidate: `build-keyboard/huntsman_firmware.bin`, 131072 bytes,
SHA-256 `2dceb0c2570bc90c4be93269d877487f39cbf8d0b402b8261b3cd6931f2b3b19`.
Linked application/data load occupies 45516 bytes; SRAMX including heap uses
11976/24576 bytes; USB SRAM uses 15488/16384 bytes. MSP is `0x04008000`,
reset vector `0x2000019d`. No application/bootloader flash region was expanded.

A separate fresh default USB-only rebuild in `build-usb-regression` is
byte-identical to the retained, previously flashed `build-firmware` binary:
SHA-256 `ef0317addedd15bc200ceba75a86536a13a51ca06a77c36d94020763a060c68d`.
The original build artifacts were not overwritten.

## Authorized flash and live CDC validation

The user explicitly requested `flash it now`. The exact candidate hash above
was verified before any mode change. Preflight found one matching keyboard,
`1532:02b0`, port `3-2.1`, device 7, with updater serial `OPENHUNTSMAN0001`.
Using only the supplied `../updater` transport/framing/flash implementation:

1. Sent one application software bootloader-entry report. The same port
   enumerated `1532:110e`, device 8.
2. Erased/programmed only the 131072-byte application image; every 64-byte
   block was acknowledged. Sent DFUExit once. No readback was performed.
3. The same port enumerated `1532:02b0`, device 9, at 480 Mbit/s. The updater
   serial query again succeeded. No unplug, forced boot mode, retry, second
   flash, bootloader write, or secondary-controller update was performed.

Driver bindings: keyboard interface 0 `usbhid`, MIDI interfaces 1/2
`snd-usb-audio`, CDC interfaces 4/5 `cdc_acm`; CDC node `/dev/ttyACM0`.
Updater interface 3 remains control-only/unbound, with working feature queries.

The first CDC read received the startup banner, an `ERR command`, valid TEST
status, and ongoing heartbeats. The script stopped validation rather than
flashing again. After reopening in raw mode, draining queued output and
sending empty lines to establish a line boundary, all checks passed. Initial
tty echo/line-discipline startup is a possible cause of the first malformed
line, not a proven firmware defect. Serial clients should drain/resynchronize
before asserting the first response.

Verified over real CDC using isolated TEST events:

- FN+Tab -> mode 1; releasing FN keeps mode 1; number 0 selects level 10;
  Escape exits with `saved=10,4 revision=1`; TEST report remains all zero.
- FN+Caps -> mode 2; releasing FN keeps mode 2; Caps toggles rapid enable
  to zero without exiting.
- Final `test reset` restores isolated defaults. Physical status reports
  `SCAN phase=0 profile=0 count=0 transfers=0 frames=0 errors=0` and
  `KEYS host=0 fn=0 mode=0`. The USB device number remains 9.

Relevant dmesg timestamps:

```text
[88676.348635] usb 3-2.1: New USB device found, idVendor=1532, idProduct=110e
[88692.262686] usb 3-2.1: New USB device found, idVendor=1532, idProduct=02b0
[88692.386382] hid-generic 0003:1532:02B0.004E: input,hidraw8: USB HID v1.11 Keyboard
[88692.387447] usb 3-2.1: Quirk or no altset; falling back to MIDI 1.0
[88692.388323] usbhid 3-2.1:1.3: couldn't find an input interrupt endpoint
[88692.389096] cdc_acm 3-2.1:1.4: ttyACM0: USB ACM device
```

The log also contains usbfs interface-not-claimed warnings for feature queries,
as at the USB-only checkpoint. No enumeration failure was observed. These
checks validate software-injected editor events on the MCU, not physical keys,
ASIC timing, non-neutral host NKRO, or long-duration stability.

## Deliberate limits and next hardware gate

- Settings are RAM-only. Production profile storage lies outside the owned
  application image; no persistent profile erase/program is implemented.
- Defaults use the recovered generic template (actuation 4, rapid 4), not
  the keyboard's saved profile. Production preset-specific overrides such as
  WASD-only rapid trigger are not imported. Optional persistent calibration
  overrides are likewise not read.
- LED colors/indicators, media/system/profile actions, alternate attached
  controller interface 2, macros and advanced analog modes are not implemented.
- The unchanged 16-byte NKRO descriptor covers usages 0x04–0x73 plus modifiers.
  Some international/JIS-specific usages above 0x73 are not reported; testing
  common keys on three maps is not validation of every international key.
- Editor entry clears previous host actions to prevent stuck keys. This is
  an explicit safety addition, not a claim of identical production reporting.

The authorized application flash and isolated CDC checks are complete. Preserve
the USB-only binary, do not touch bootloader/secondary firmware, and do not use
manual recovery as a test step. The next physical test is to explicitly start
scanning with host keys off, inspect samples/trace, and only then arm NKRO.
Physical FN/Tab/Caps presses require the user's participation. No automatic
reflash or mode-change retry is part of this candidate.
