# Production-derived per-key travel lighting

Status: flashed once with explicit authorization on 2026-09-05. USB, CDC,
optical scanning and LED transfer completion passed the checks below.
**Visible illumination and physical travel proportionality remain unverified.**
The retained `build-stream` image is unchanged.

## Behavior and limits

The `travel-lighting` preset brings up USB first, automatically starts one
optical scan attempt after USB configuration, then initializes lighting once
ASIC layout discovery succeeds. It does not require CDC to be opened or a
`scan start` command. Host NKRO keystrokes remain separately gated by `keys on`.
Diagnostic/USB-only presets retain their existing startup policy.

Each key is white with 8-bit PWM proportional to its endpoint-normalized raw
reading. It does not use pressed/released state, the FN editor, actuation
threshold, rapid-trigger hysteresis, gamma correction, or the keyboard engine's
low-level deadband:

```text
PWM = round(255 * (upper - raw) / (upper - lower)), clamped to 0..255
raw >= upper: off; raw <= lower: maximum white
```

Invalid input, unsettled scanning, stopped/faulted scanning, USB unconfigured,
or no new scan for 100 ms requests a black frame. A new full lighting frame is
started at most every 40 ms, corresponding to production's alternating
20 ms controller slots. Each upload snapshots the newest desired frame;
there is no historical-frame FIFO. The primary and secondary controller
uploads use the same frozen snapshot.

**This is linear in production-normalized optical travel, not a validated
linear millimeter measurement or perceived-brightness curve.** The existing
scanner attempts the original ASIC endpoint calibration. If rejected, it keeps
production defaults 2240/3360 from `0x20016464`. The earlier physical test
reported `calibrated=0`; using defaults may leave dark/saturated portions of
the physical stroke. This work does not pretend that calibration succeeded.

Production additionally reads CRC-checked persistent calibration blocks
(`0x2001ad30`: flash offsets `0x49000`, `0x49600`, `0x49c00`) and applies
overrides in `0x20015dec`. Those persistent reads/overrides are not implemented
here; no new flash-controller commands, factory-region access or calibration
writes were introduced. Accurate full-stroke millimeter proportionality
remains dependent on recovering/validating those endpoints or physical
measurements. The LED mapping/protocol and PWM arithmetic are independently
testable without assuming that accuracy.

## Production evidence

Reference: read-only `../extracted_firmware/analysis/primary_app_listing.asm`,
`primary_app_decompiled.c`, and hash-pinned raw application
`Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin` (SHA-256
`d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27`).
The broken sibling implementation was not consulted.

| Production entry/data | Recovered behavior |
| --- | --- |
| `0x2000ddc8`, `0x20002410`, `0x20002ddc` | FLEXCOMM1/I2C1, FRO12M, 400 kbit/s; P0_13/P0_14 IOCON `0x101` |
| `0x2000cfa8`, `0x2000dfa4` | Write P0_8 and P0_26 low, wait 10 ms, both high, wait 5 ms, initialize controllers |
| `0x2001d4ff`, `0x2000cb14` | Stored address bytes `a0 d8` are shifted right before SDK calls: 7-bit addresses `0x50`, `0x6c` |
| `0x200092d0(0,0)` | Primary initialization, including page unlock/select and 24 channel-enable bytes |
| `0x2000de70(1,0)` | Secondary initialization only for ASIC layout 3 (JIS) |
| `0x2000e204`, `0x200092d0(0,3)` | Primary page maintenance after 26 primary update slots |
| Runtime `0x04000004`, 75 nine-byte records | Key ID, row/column, controller, individual R/G/B channel indices, effect metadata |
| `0x2000cbf0`, `0x2000e410` | Separate ANSI/ISO FN/right-Alt patches for scan and lighting maps |
| `0x200098b0` | Actual normal-effect color-to-controller renderer used for differential tests |
| `0x20016464`, `0x20015dec`, `0x2000e354` | Default endpoints, endpoint calibration, inverse raw normalization |

The RGB channels are **not packed per key**. For example A's red/green/blue
channels are `a1/91/b1`, and ANSI FN (raw index 43) uses `ae/9e/be`, not the
unpatched FN record's `af/9f/bf`. ANSI uses 183 primary channels, ISO 186;
JIS uses 183 primary plus 12 secondary channels. Unmapped channels stay zero.

`tools/lighting_reference_tables.py` executes the original scatter initializer,
the two layout patches and controller initialization/maintenance routines.
It emits `src/lighting_reference_tables.c`, not a handwritten physical-row
guess. The observed register programs are:

- Primary: `fe=c5`, `fd=03`, `00=05`, `01=ff`, `0f=07`, `10=07`,
  `fe=c5`, `fd=01`, 192 zeros at `00`, `fe=c5`, `fd=00`, **24 ff bytes
  at `00`**, `fe=c5`, `fd=01`.
- JIS secondary: `2f=00`, wait 5 ms, `00=01`, twelve `10` bytes at `17`,
  `26=00`, `27=00`, twelve zeros at `04`, `13=00`.
- Primary maintenance: `fe=c5`, `fd=00`, 24 ff bytes at `00`, `fe=c5`,
  `fd=03`, `00=01`, `01=ff`, `e0=01`, `e1=e2=e3=00`, `e0=00`,
  `fe=c5`, `fd=01`.

Steady updates write 192 primary bytes at `00`; JIS additionally writes twelve
bytes at secondary `04`, then `13=00`. No speculative registers are used.
The pins' electrical roles are not inferred beyond this recovered sequence.

## SDK and failure handling

The build uses the existing official MCUXpresso Installer-selected NXP source
snapshots and Arm GNU 14.2.1 toolchain. No vendor sources were modified. LED
GPIO, clock, FLEXCOMM and I2C operations use the SDK. The old blocking
`src/lighting.c` is not linked into this candidate.

Production uses DMA channel 7 and retries/reinitializes on errors. This candidate
deliberately uses the SDK's **nonblocking interrupt** I2C API: its DMA error
paths call `DMA_AbortTransfer`, which can busy-wait indefinitely. LEDs have
FLEXCOMM1 IRQ priority 3, below USB and optical DMA. LED initialization never
calls `DMA_Init`, so it cannot reset the scanner's shared DMA controller.

An I2C error or 20 ms transfer timeout latches one fault and disables LED I2C
interrupts; the driver handle and payload remain allocated. There is no bus
abort spin, retry, GPIO cycle, MCU reset, or reinitialization. USB/CDC and optical
scanning remain serviced. On a bus fault, the last already-applied LED values
may remain visible: blacking them cannot be guaranteed over a failed bus.
`light on` does not clear a latched fault or restart hardware.

## Build and commands

```sh
cmake --preset travel-lighting
cmake --build --preset travel-lighting
cmake --build --preset host-tests --target audit-lighting
cmake --build --preset travel-lighting --target audit-lighting
```

The audit targets require the Python dependencies in `tools/requirements-audit.txt`.
They neither open nor flash a device. Output is
`build-lighting/huntsman_firmware.bin`, exactly 131072 bytes, linked at
`0x20000000`. Existing USB/keyboard/stream images are preserved. Reviewed hash:
`4951843f21c36363627f3e55735ca101dfd839fc8b434503af4382a8366bd7b9`.
Application load 51276 bytes; SRAMX including heap 18696/24576 bytes; USB SRAM
15488/16384 bytes. Bootloader and other flash regions are unchanged.

Runtime diagnostics are:

```text
stream off
light status
scan status
scan sample 20
light off
light on
stream on
```

`light status` reports phase, requested state, layout, transfers, complete frames,
errors and accepted calibration count. Phases: 0 off/not started, 1 low wait,
2 high wait, 3 primary init, 4 secondary init, 5 running, 6 maintenance, 7 fault.
`light off/on` changes the desired effect without hardware reinitialization.
As before, text replies are suppressed while the binary stream owns CDC.

## Validation

- 262,144 raw uint16/PWM comparisons: monotonic linear rounding, saturation,
  endpoints and invalid values. No hidden binary key threshold.
- 188 single-sensor isolations and 36 mixed frames: the application's entire
  204-byte LED output equals the executed production ARM renderer for all
  three layouts, including patched FN/right-Alt and JIS secondary channels.
- Actual compiled SDK I2C interrupt transfers compared byte-for-byte with
  executed production init/maintenance programs. Synthetic optical DMA runs
  concurrently through the real keyboard engine and SDK USB callbacks.
- Automatic startup, 400 kbit/s divider/pins, JIS-only secondary, continuous
  white scaling, off/on, invalid/stale/stop blanking and neutral host reports.
- Held transfers retain their original payload while newer scans arrive;
  the next upload uses the newest frame.
- NACK and stalled-transfer injection: one latched fault, no retry/GPIO cycle,
  continued optical scans and responsive CDC, no reset intent.
- Full USB startup/PHY/alignment/updater, NKRO/editor and raw-stream regressions
  pass on the lighting ELF. Existing native keyboard/reference audits also run.

The tests above are software/register-model results, not measurements of
electrical timing, keycap illumination, physical travel, power draw or
interrupt latency on the connected board.

### Authorized hardware flash and checks (2026-09-05)

- Flashed the reviewed SHA-256 above using the supplied updater's
  `flash_app_image` once: application-only erase, all 2048 64-byte blocks
  acknowledged, then DFU exit. No flash readback is available through this
  workflow; acknowledgments are not an independent byte-for-byte readback.
- One software bootloader entry; no retry, manual reset, GPIO recovery or
  secondary/factory-region programming. Port `3-2.1` changed from application
  device 11 to bootloader device 12, then application device 13.
- Application returned at 480 Mbit/s with keyboard, MIDI and CDC drivers;
  updater serial feature response was unchanged. Kernel messages showed the
  expected MIDI 1.0 fallback and control-only HID interface warning, with no
  additional disconnect/enumeration failure during validation.
- CDC `light status`: profile 1, on, running/maintenance phases 5/6,
  transfers increasing from 780 to 1014, complete frames from 500 to 650,
  errors 0. These counters establish completed controller transactions,
  not visible keycap output.
- CDC `scan status`: 61 sensors, settled and valid, errors 0; frames
  increased from 32024 to 39742. Calibration remained 0. Sample index 32
  reported raw 3921 with fallback endpoints 2240/3360.
- With lighting running, a 3.0-second binary CDC capture decoded 5683
  complete 61-sensor frames, no sequence discontinuities or invalid frames,
  raw range 3782..4018. Observed delivery was about 1.89 kframes/s, not 8 kHz.
- Left scanning, lighting and binary streaming enabled; host keystrokes
  remain disabled. USB device number remained 13 throughout the checks.

Physical confirmation of per-key illumination is still required. In
particular, rejected calibration means this test does not establish linear
brightness over the entire physical key stroke.
