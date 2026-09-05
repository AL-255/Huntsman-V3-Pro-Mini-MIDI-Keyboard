# USB-only bring-up — 2026-09-05

Scope: USB only. No optical, lighting, SPI, or I2C runtime is linked into the
default image. The previous full-application code is retained but excluded
by `HUNTSMAN_USB_ONLY=ON`. The offline audit was followed by one explicitly
authorized hardware flash. High-speed enumeration, driver binding, updater
information queries, and CDC debug output succeeded; details below.

## Concrete defects corrected

1. **DCI bus-reset alignment** (previous checkpoint): GCC combined adjacent
   bytes into `STRH` at USB SRAM `0x401000a7`. `-mno-unaligned-access` prevents
   this; the original byte stores are recorded in [BRINGUP_AUDIT.md](BRINGUP_AUDIT.md).
2. **Prebuilt libc alignment** (new): the actual 43-byte keyboard HID report
   descriptor transfer faulted in newlib `memcpy`, at an odd-address halfword
   tail store to `0x401001a9` (intermediate image PC `0x20005384`). Compiler
   flags cannot change prebuilt newlib assembly. The linked `__wrap_memcpy`
   uses aligned words only when both pointers are aligned, and byte tails,
   following production `0x200004a2..0x200004c4`. Volatile access widths prevent
   compiler folding back into unsafe memcpy. Tests cover every mod-4 source
   and destination alignment, 19 lengths up to 512, guards, and return value.
3. **PHY workaround fidelity**: executing production `0x20005c84` exposed
   differences hidden by its decompiled C. Actual `0x20005ca2` reads INFO
   before the guard tests. Disconnect is two separate writes at `0x20005d20`
   and `0x20005d28`. Reconnect separately sets FORCE_FS when needed, replaces
   change bits 24..27 with 1, then writes the reconnect value. The earlier
   reconstruction omitted intermediate MMIO writes. The corrected ARM code
   matches production's frame-read, USB-write, and wait traces in all four
   modeled branches. This is register-sequence equivalence, not PHY validation.
4. **Extra clock prerequisite**: board initialization borrowed external-system
   clock setup from production's 100/150 MHz branches. The SDK version waits
   indefinitely for XO_READY there. Production's 96 MHz branch at `0x20000620`
   does not call it. That extra setup is removed; USB still uses the recovered
   16 MHz reference through NXP USB PLL/PHY initialization. This removes an
   unsupported prerequisite, not a claim that XO_READY caused a recorded
   hardware failure.
5. **Updater status ordering**: a 20 ms delay after command-data receipt could
   reset before status acknowledgment. A link-time wrapper observes vendor
   DCI notifications without editing vendor sources. Only an EP0 IN (`0x80`)
   zero-length completion after the accepted command starts the delay. New
   SETUP/bus reset before ACK cancels it. The vendor Chapter 9 callback leaves
   an InvalidRequest return on status-only completion, so the controller's
   completion event—not that return code—is used. Tested at both speeds:
   no reset intent after 1000 ms without ACK, cancellation, 19/20 ms boundary,
   and no deadline extension by a later unrelated status completion.
6. **USB callback state**: send-busy flags are established before submission,
   with interrupt masking spanning state checks, buffer preparation and
   submission. CDC debug submission uses the same protection. GET_CONFIGURATION
   and GET_INTERFACE are implemented, and all six alternate-zero requests
   pass. NKRO is now report-only HID; an unimplemented boot protocol is no
   longer advertised.

The application continues to use NXP USB classes/DCI/IP3511 and peripheral
drivers. Vendor sources are unchanged. The two link wrappers are application
integration code. The supplied `../updater` sends the entry SET_REPORT and
closes the handle; it does not require a subsequent GET_REPORT to enter boot.

## Offline checks

Install `tools/requirements-audit.txt`, then:

```sh
cmake --preset firmware
cmake --build --preset firmware --target audit-usb

python3 tools/test_usb_chirp_arm.py build-firmware/huntsman_firmware.elf \
  --reference '../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin'

# Locally retained failing ELF, not a file shipped in Git:
python3 tools/test_usb_arm.py /tmp/huntsman-before-alignment-fix.elf \
  --expect-reset-alignment-fault
python3 tools/test_usb_arm.py /tmp/huntsman-before-alignment-fix.elf \
  --expect-libc-alignment-fault
```

The reference is read-only and its SHA-256 is checked against the production
image identified in the earlier audit. The two negative controls respectively
require the old DCI halfword fault and newlib's odd-tail halfword fault; an
arbitrary emulator error is not accepted.

The positive tests execute the actual Cortex-M33 instructions and cover:

- Reset_Handler/SystemInit, LMA-to-VMA data copy, BSS/heap/stack clearing,
  MSP, VTOR, interrupt vectors and interrupt unmasking before main;
- Core clock selection, SysTick reload, USB peripheral reset ordering,
  PORTMODE, 16 MHz PLL divider selection, PHY power/clock writes and RAM clear;
- CTIMER3 setup, priority 0 versus USB priority 1, and vector-to-SDK-to-callback
  dispatch, plus the production-comparison chirp branches;
- USB1_IRQHandler through the actual controller and class stack: device,
  configuration, string and HID report descriptors, address/configuration,
  all six alternate-zero interfaces, NKRO, MIDI, CDC, and updater queries;
- Updater status-stage gating and aborted control-transfer cases;
- USB SRAM copy alignment, bounds and content.

The startup model supplies **synthetic invalid factory trims** and automatic
completion of FLASH CMD_SET_READ_MODE. It does not capture silicon calibration
or validate regulator voltage. The chirp model completes requested timer waits
immediately and supplies selected frame values. There is no modeled oscillator
lock, elapsed electrical time, real NVIC preemption, host scheduling, bootloader
execution, or physical reconnect. Transport reset intent is intercepted before
board_enter_bootloader; these tests cannot establish the physical update round
trip. Hardware observations below were collected separately with authorized
privileged access; they are not results of the emulator.

## Authorized hardware trial

On 2026-09-05, the exact binary below, built from commit `7a84ca4`, was
flashed once using the supplied `../updater` implementation. Preflight checked
the binary size/hash and a single `1532:110e` bootloader on USB port `1-1`.
The operation erased/programmed only the 128 KiB application region, received
acknowledgments for all program blocks, and sent DFUExit. No flash readback was
performed. The bootloader and secondary-controller firmware were not written.

Observed after DFUExit:

- Linux enumerated `1532:02b0`, device number 19 on the same port, at 480 Mbit/s.
- Interface 0 bound to `usbhid`/`hid-generic` as a keyboard (`hidraw8`).
- Interfaces 1/2 bound to `snd-usb-audio`; ALSA listed the device as card
  `MIDI`, named `Huntsman V3 Pro Mini MIDI`.
- Interfaces 4/5 bound to `cdc_acm`, exposing `/dev/ttyACM0`.
- Supplied-updater information queries returned version `0201` and serial
  `OPENHUNTSMAN0001`. The USB string descriptor independently reports
  `OPENHUNTSMAN00000000`; these are currently distinct placeholder values.
- A six-second raw CDC read at 115200 baud received 114 bytes: six complete
  `USB service alive\r\n` messages. The USB device number remained 19.

Relevant kernel messages (monotonic timestamps):

```text
[85249.757120] usb 1-1: new high-speed USB device number 19 using xhci_hcd
[85249.883527] usb 1-1: New USB device found, idVendor=1532, idProduct=02b0, bcdDevice= 2.01
[85249.969222] hid-generic 0003:1532:02B0.0047: input,hidraw8: USB HID v1.11 Keyboard [OpenHuntsman Huntsman V3 Pro Mini MIDI] on usb-0000:07:00.3-1/input0
[85249.970289] usbhid 1-1:1.3: couldn't find an input interrupt endpoint
[85250.020746] cdc_acm 1-1:1.4: ttyACM0: USB ACM device
[85250.036163] usb 1-1: Quirk or no altset; falling back to MIDI 1.0
[85250.879370] usb 1-1: usbfs: process 501759 (python3) did not claim interface 3 before use
```

Interface 3 is intentionally control-only; its missing interrupt endpoint
message did not prevent updater information queries. The MIDI fallback and
usbfs interface-claim messages are retained here rather than calling the log
warning-free. No new enumeration failure appeared in the post-flash log.

No second flash, application-to-bootloader command, explicit USB reset, or manual
recovery was used in this trial. MIDI payload traffic, non-neutral NKRO key
reports, full-speed hardware operation, suspend/resume, long-duration stability,
and application-to-bootloader recovery were not physically tested.

## Tested image and remaining boundary

Binary SHA-256:
`ef0317addedd15bc200ceba75a86536a13a51ca06a77c36d94020763a060c68d`.
Image size 131072 bytes; linked content 23456 bytes; USB SRAM 15488/16384;
MSP `0x04008000`, reset vector `0x2000019d`. No optical/lighting/SPI/I2C
runtime symbols remain in the linked image. Newlib file-syscall linker warnings
remain; the build is not represented as warning-free.

The hardware trial establishes initial high-speed USB operation on this host,
not general USB compliance or a proven software recovery path. Other inherited
hardware states, PHY conditions, arbitrary interrupt interleavings, and broader
USB compliance/error paths remain untested. No automatic flash is part of any
build/test target. Another hardware trial requires explicit user approval;
do not silently return to speculative flash/recovery cycles.
