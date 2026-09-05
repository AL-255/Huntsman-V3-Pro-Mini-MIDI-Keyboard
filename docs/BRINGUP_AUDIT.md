# Bring-up audit — 2026-09-04

## Status and correction of approach

This is a development checkpoint, not a release or permission to flash.
No connected-device operations were performed during this audit pass.

Earlier bring-up used successful compilation as a gate for flashing, changed
USB speed/servicing without first locating the failing instruction, and
underestimated the cost of manual bootloader recovery. That approach was
wrong. Host connection messages establish neither application health nor a
working software recovery path. Reappearance in bootloader must not be
attributed to an automatic application reset when user recovery may explain it.

The revised gate is instruction-level comparison against the known-working
production image, an offline reproduction of each tractable defect, and an
explicit account of what remains unverified. Physical recovery is not an
acceptable substitute for this analysis. Another flash requires resolved
blockers and explicit user approval.

## Reference and artifact identity

The board-behavior authority is the supplied production application:

`../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin`

SHA-256: `d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27`.

Addresses below refer to that image linked at `0x20000000`, as recorded in
`../extracted_firmware/analysis/primary_app_listing.asm` and
`primary_app_decompiled.c`. The disassembly takes precedence over narrative
RE notes; the notes contain at least one material clock error. The extracted
directory was read only. The old broken implementation was not consulted.

The saved pre-fix ELF is `/tmp/huntsman-before-alignment-fix.elf`, SHA-256
`0d1b08fdd829137535135356607a28ae8d5eb741cda6b58e78e6902038ec8432`.
Its application binary, identified during the earlier failed trial, had hash
`aeb79aff5624f631522982d186196ce0cd8adb5ea4906c07062192c4d4b5d8fe`.
The temporary ELF is a local negative-control artifact, not included in Git.

The corrected, offline-tested binary at this checkpoint has SHA-256
`374be3f52639af532fa463158dccb4e2f9e023ef68f74d28cb013fa9d33a099b`.
It is 131072 bytes including padding; linked application content uses 30436
bytes and USB SRAM uses 15488 of 16384 bytes. Initial MSP is `0x04008000`
and the Thumb reset vector is `0x20000195`. This binary was **not flashed**.
The link succeeds with newlib/nosys warnings for unimplemented file syscalls;
those warnings are not being presented as a warning-free build.

## Demonstrated USB bus-reset defect

The pre-fix ELF contains this in `USB_DeviceNotificationTrigger`, inlining
the NXP DCI reset handling:

```asm
20001dde: mov.w  r2, #0x100
20001de2: ldr    r0, [r0, #12]
20001de4: strh.w r2, [r4, #167]
```

Executing initialization and then bus reset sets `r4 = 0x40100000` (the
allocated USB device state), so the halfword store targets `0x401000a7`.
It merged adjacent byte fields into an odd-address halfword access.

USB SRAM is in the default Device-memory region. Unaligned Device-memory
access faults regardless of the normal-memory unaligned-access setting;
clearing `CCR.UNALIGN_TRP` is not a fix. This architectural rule is specified
by Arm, not inferred from the keyboard schematic. See the
[Armv8-M Architecture Reference Manual, DDI 0553A.f, B5.16](https://documentation-service.arm.com/static/5f8efff7f86e16515cdbe5f9).

The production allocator at `0x20003f0a` likewise selects `0x40100000`.
The corresponding production reset routine instead uses byte stores:

```asm
20006dc4: movs   r0, #1
20006dc6: strb.w r0, [r4, #0xa8]
20006dca: movs   r5, #0
...
20006dd2: strb.w r5, [r4, #0xa7]
```

The bad store precedes control-endpoint initialization. It therefore explains
how connection can be detected while even the device descriptor cannot be
returned. Polling the same ISR cannot avoid the store.

Correction: `-mno-unaligned-access` is applied to application and core ARM
compilation in `CMakeLists.txt`. The rebuilt reset path has separate byte
stores, and the tested USB paths no longer make unaligned USB SRAM accesses.
Vendor source files were not patched to hide this build configuration error.

Evidence limit: the failing compiled instruction and its effective address
are demonstrated offline. No fault-register capture from the physical device
was obtained. This is a concrete defect consistent with the observed
enumeration failure, not proof that it was the only hardware failure. MPU
state inherited from the bootloader has not been captured; the analysis uses
the architectural default memory map and the absence of MPU setup in this
application.

## Clock claim withdrawn

An earlier claim that production uses a 12 MHz USB reference was wrong.
Production `0x20004b10` constructs:

```asm
20004b12: movw r4, #0x2400
20004b16: movt r4, #0x00f4
```

That is `0x00f42400 = 16000000`, passed to the PLL routine at `0x20004b1e`
and PHY initialization at `0x20004b32`. The application retains 16 MHz. No
clock change is justified by the erroneous 12 MHz narrative in the RE notes.

Forced full-speed operation and the polling fallback have been removed.
The production-style USB IRQ path and recovered conditional PHY chirp
workaround remain. The offline USB test stubs that workaround; it does not
verify PHY behavior or timer interrupt delivery.

## Other corrected USB defects

- The serial string advertised 46 bytes but occupied 42. Its header is now
  42, and all eight string objects are checked against ELF symbol sizes both
  post-link and during actual compiled control transfers.
- The NXP PCM AudioStreaming class accepts subclass 2 and is not a MIDI
  Streaming (subclass 3) bulk class. Using it left MIDI endpoint callbacks
  uninstalled. MIDI now uses the vendor DCI endpoint-init/send/receive APIs;
  PCM Audio class registration is removed. Tests require the callbacks to
  exist and exercise completion and rearming, not just descriptors.
- CDC receive completion does not rearm a cancelled transfer or a detached
  device. Configured-transfer/reset cases are included in the offline test.

## Reproducible offline checks

After installing `tools/requirements-audit.txt` in a Python environment:

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset firmware
cmake --build --preset firmware --target audit-usb

# Local saved artifact, when still available:
python3 tools/test_usb_arm.py /tmp/huntsman-before-alignment-fix.elf \
    --expect-reset-alignment-fault
```

The negative control requires the same odd-address halfword access and
reports PC `0x20001de4`; an arbitrary emulator failure is not accepted.

`test_usb_arm.py` loads ELF segments at their virtual addresses and executes
the linked ARM application, NXP class, DCI, and IP3511 code. A small register
model supplies USB reset/setup/endpoint-completion events and implements the
W1C/endpoint-skip behavior used by those paths. A memory hook enforces
Device-memory alignment because Unicorn does not enforce it by itself.

At both modeled full and high speed, the corrected image passes:

- Device, complete 210-byte configuration, and all eight string descriptors;
- SET_ADDRESS and SET_CONFIGURATION;
- MIDI bulk IN/OUT, callback registration, completion, and receive rearming;
- CDC line coding, DTR, IN/OUT, and receive rearming;
- 16-byte NKRO report transfer and completion;
- All eight updater information queries through 90-byte HID SET_REPORT /
  GET_REPORT, including 64+26-byte control data stages and checksums;
- Bus reset after configured transfers, followed by a device-descriptor read.

Native tests separately cover report packing, scan settling/hysteresis, and
updater protocol framing. The post-link validator checks image size, MSP,
reset vector, USB interface/endpoint layout, report sizes, and string bounds.

Limits: the harness starts with ELF data already loaded, does not execute
Reset_Handler/SystemInit, and stubs board clocks, delays, IRQ enable, and
PHY recovery. It calls the ISR code directly, not through NVIC exception
delivery. It does not model electrical signaling, USB host timing, arbitrary
IRQ interleavings, inherited bootloader state, optics, LEDs, or watchdogs.
It does not execute a physical bootloader transition or update flash. Passing
these tests is not a claim of physical enumeration or complete USB compliance.

## Open board discrepancies — flashing blockers

These are observed differences, not speculative diagnoses of the USB failure.
They remain unfixed in this checkpoint so they cannot be mistaken for
validated board support.

| Production evidence | Current gap / required work |
| --- | --- |
| `0x2000ca68` initializes GPIO 8/26 low. `0x2000cfa8` calls `0x2000dfa4(0,0)`, waits 10 ms, calls `(0,1)`, waits 5 ms, then initializes the primary LED controller. `0x2000dfa4` writes both GPIO byte aliases through `0x20002668`. | Application leaves both outputs low. Reconstruct the complete startup ordering before implementing the missing transition. Their electrical roles (power/reset/etc.) are **unknown**; the call graph ties this sequence to LED initialization, not proof of an optical reset. |
| `0x200093e6..0x2000940c` copies and writes 24 bytes from `0x2001b458 + controller_index * 24` on primary LED page 0. | Application changes to page 0 and immediately back to page 1, omitting this table. Recover the exact table and error propagation. |
| `0x2000de7c..0x2000de80` gates secondary LED initialization on profile 3. | Application initializes and services address `0x6c` unconditionally. Profile discovery must determine whether that path is used. |
| `0x2000dcbc` / shared body `0x2000d838` sends B6/2 followed by A2/0 (9 bytes), reads metadata/profile, and selects 61/62/65 items. | Application uses fixed 61-item scans and B6/1, with no metadata/table startup sequence. Recover the profile and mode chronology. |
| `0x2000cf30` copies profile default tables from `0x2001d870` (366 bytes), `0x2001d574` (372), or `0x2001d6e8` (390). Other routines load ASIC tables at runtime. | The assertion that mappings cannot be recovered from the application was unjustified. Key and LED orders remain provisional; recover table consumers and semantics. |
| `0x2000dccc` bounds SPI waits using `SystemCoreClock >> 12` and feeds the watchdog while waiting. | Current blocking SDK SPI/I2C paths default to unlimited software retries. I2C hardware timeout configuration alone does not establish a bounded main-loop stall. Implement and test bounded progress/error recovery while preserving USB maintenance. |

The mutually exclusive GPIO 29/30 route transitions are observed at
`0x20017528` and `0x20017540`; their electrical function is not established.
The internal watchdog setup/feed routines at `0x2001b0a0` / `0x2001b148` do
not justify assuming that no external watchdog or power-management dependency
exists. Current code only feeds an already-enabled internal watchdog.

## Remaining application/USB review

The following are software issues to resolve offline before a hardware trial:

- The keyboard advertises boot protocol but only constructs an NKRO report.
  Implement the boot-protocol path or stop advertising it.
- Main sends only on scan changes and ignores a busy return; a release can
  be lost. Add pending-report retry and reconnect state synchronization.
- Send-busy flags are assigned after submission; interrupt completion can
  race the assignment. Audit CDC debug-ring concurrency at the same time.
- Audit unsupported interface/configuration/control requests and error paths,
  including reconfiguration and endpoint initialization failures.
- The boot command uses a 20 ms delay, not evidence of status-stage ACK.
  Prove the control-transfer completion ordering against `../updater` before
  relying on computer-initiated recovery.
- Complete startup/clock/PHY/interrupt and off-chip sequence comparison. The
  offline USB harness deliberately does not cover those boundaries.

Do not collapse this list into a claim that the USB fix makes the firmware
ready. Next work is safe static reconstruction and offline tests, not another
speculative device trial.
