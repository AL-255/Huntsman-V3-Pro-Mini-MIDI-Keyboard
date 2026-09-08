# Device calibration storage

Calibration uses **only two whole 512-byte pages at physical addresses
0x7d400 and 0x7d600**. The beginning of configuration storage, including the
serial number and primary settings at 0x49000..0x49400, is not an erase target.

## Evidence and ownership

On 2026-09-05, two independent controller reads of 0x49400..0x7d800 matched,
without read errors. Both selected pages contained exactly 512 FF bytes.
The original allocator's block chain at 0x54400 identifies five allocated
0x580-byte blocks followed by a free block starting at 0x55f80 with size
0x29480 (ending at 0x7f400). The selected pages are inside its free payload,
not its header or footer. The final footer lies outside the conservative
read boundary and was not read; it is not claimed verified.

The private tail backup's SHA256 is
`dcc23b75e95268474d58fe4c11082dbf032320afa6cd82aa12386f4511523852`.
The primary-settings backup is
`023f502cf741e4d577b88a2064a3a897fe0c544e61cfc7273cb1a4678b183114`.
Both are ignored by Git. No serial-number bytes are published.

The FF tail inside the primary settings' second page is deliberately **not**
used: erasing it would also erase existing settings in that same page.
The independent application does not use the original allocator. Returning to
stock firmware may reclaim or clear the free block and lose our calibration.
We do not alter allocator boundary tags or promise that stock preserves our data.

## Record and recovery

Each HKC1 page uses this little-endian layout:

| Offset | Field |
| --- | --- |
| 0 | Four-byte `HKC1` magic |
| 4, 5, 6, 7 | uint8 version 1, layout, sensor count, reserved zero |
| 8 | uint32 generation |
| 12 | uint32 ownership marker `0x314c4143` |
| 16 | 65 uint16 lower bounds |
| 146 | 65 uint16 upper bounds |
| 276..507 | Reserved FF padding |
| 508 | IEEE CRC32 over bytes 0..507 |

Unused sensors are zero. Bounds must have at least 512 counts of range and
lie within the valid ADC domain. Layout identity must match.

Boot reads both pages and selects the newest valid matching generation,
including uint32 rollover. No boot-time erase/program occurs. Without a valid
record, existing factory-derived endpoint behavior remains in use.

A complete calibration writes only the inactive page, leaving the prior
record untouched. Before erase, the target must read successfully and be
entirely FF or carry our recognizable HKC1 ownership header. Unknown contents
or ECC errors cause a save failure, not an erase. A recognizable torn record
may be replaced. An unreadable page after an interrupted erase is not
automatically reclaimed; the previous readable record can still load.

The full page is erased, programmed, read back and compared byte for byte,
including CRC, before RAM endpoints and the active generation are updated.
Power failure during the first save can leave no valid record, in which case
factory-derived endpoints are used. After an existing valid save, interrupted
inactive-page writes leave the older valid record available. This is a
software-level recovery design, not a claim that power-cut silicon testing
has been performed.

## Controller boundary

The application uses NXP SDK register definitions/status codes and the
controller sequence established by the original working application:
command 4 erases one page, 32 command-8 loads populate its buffer, and command
12 programs it. Status polling is bounded; a controller timeout latches out
further commands. Interrupt state is preserved, cache is flushed, and the
existing watchdog is serviced before/after operations. Code and stack execute
from RAM. SDK ROM-wrapper calls are excluded; the controller adapter is
checked against original ARM register transactions.

The adapter accepts a slot number, never an arbitrary write address.
Invalid slot, geometry, clock or record rejects before erase. CDC exposes no
raw erase/program command. Only completing every key in calibration can save.
Fn+R previews `RESET`; release opens `RESET?` with full-brightness green Y/red N.
After all keys are released, a fresh Y press invokes the bounded clear operation;
N cancels without erasing. Pre-held Y cannot confirm and simultaneous Y/N cancels.
Both pages must be blank or recognizable HKC1 records before any erase; the
older slot is cleared first and each erase is read back. Unknown contents or
controller errors stop clearing. Empty pages are skipped. Successful clearing
removes saved calibration and restores application defaults on neutral input;
it does not erase factory/serial data or reboot USB. See [RESET](FN_MENU.md#reset-and-flash-boundaries).

The original 1 KiB reservation at image offsets 0x1fc00/0x1fe00 remains FF.
Although readback establishes physical application base 0x8000, we do not
modify its image/checksum bytes for persistence. Bootloader, factory/security,
secondary ASIC and the serial-number pages remain outside write scope.

## Validation

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset keyboard-fn-menu
cmake --build --preset keyboard-fn-menu
# Optional offline ARM dependencies and original reference required:
python3 -B tools/test_calibration_arm.py \
  build-keyboard-fn-menu/huntsman_firmware.elf --reference /path/to/original.bin
```

Native tests cover simultaneous 61/62/65-key holds at 8 kHz, independent
movement/release, timing, layouts, rollover, noise, cancellation, CRC damage,
unexpected page contents, error handling and all 512 byte-cut points in an
interrupted inactive-page write. ARM tests compare exact register writes with
executed original erase/program instructions, then exercise compiled Fn+C,
CDC commands, complete sequential and parallel simulated 61-key acquisition,
save and reboot loading.
These tests never access the real keyboard. See [calibration operation](CALIBRATION.md)
for physical validation status and limitations.

The Fn menu does not change this record format or write boundary. Brightness
and trigger-point edits are RAM-only; trigger commits use loaded calibration
bounds without changing the stored record.
