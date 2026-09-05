# Application-owned configuration allocation

Status: **space reserved and build-validated; persistent save is not implemented
or enabled. No hardware flash or configuration writes were performed.**

The stock configuration, profiles, allocator records, macros, factory data,
calibration and bootloader must remain untouched. No part of their storage is
allocated to this application.

The application linker now limits code and initialized data to the first
127 KiB of the existing 128 KiB updater image. Its last 1 KiB is reserved:

| Slot | Image offset | RAM execution-image address | Size |
| --- | --- | --- | --- |
| A | `0x1fc00` | `0x2001fc00` | 512 bytes |
| B | `0x1fe00` | `0x2001fe00` | 512 bytes |

These are **not physical flash addresses**. Do not pass them to a flash driver.
The two slots provide room for a future alternating-record format containing
all 65 press/release pairs (260 bytes), layout identity, version, generation
and integrity fields. No record format or power-loss guarantee is implemented
by this allocation alone.

The section is `NOLOAD`: startup does not initialize it. Existing binary
padding fills both slots with FF in every fresh 131072-byte updater image.
Linker bounds prevent application code/data from growing into the slots;
the post-build validator checks their symbols, the load boundary and FF bytes.
The updater continues to receive its original fixed-size image. A normal full
application update will replace these image bytes, so configuration retention
across firmware updates is **not** promised.

## Remaining prerequisite for erase/program

The supplied updater's `flash_app_image()` sends RAM-image addresses
`0x20000000..0x20020000` to the existing bootloader. This does not establish
the physical backing-flash address, nor which integrity checks that bootloader
performs on subsequent boots. The extracted primary application is not a
bootloader dump. Its known stock-settings write routines do not establish
either property for the application image.

Before connecting a GUI Save command to erase/program, establish the backing
mapping and boot validation behavior from bootloader evidence (or an equivalent
verified reference). A wrong mapping could erase unrelated data; changing
checksummed image bytes could prevent booting even with the right mapping.
Do not guess a physical offset, silently use stock user storage, or test these
assumptions by flashing and relying on manual recovery.

Then implement the bounded SDK flash adapter, interrupted-save recovery,
readback verification, boot-time loading, scan/USB-safe write scheduling, and
GUI acknowledgments. Until those are complete, the GUI continues to operate
with RAM configuration and its existing host JSON import/export.

## Build

```
cmake --preset keyboard-storage-layout
cmake --build --preset keyboard-storage-layout
python3 -B tools/test_image_reservation.py
```

Use this new directory; preserve earlier flashed and unflashed artifacts.

Validation on 2026-09-05: the new ARM build and all six host test suites passed.
The application load remains 56544 bytes, SRAMX use 22280 bytes and USB SRAM
use 15488 bytes. The binary is byte-for-byte identical to the preserved,
unflashed `keyboard-velocity-float` candidate: SHA-256
`cf89ed23cf9caee38ca7583402497f12e36e824010022cc69731dda2bc33f6f9`.
The reservation changes linker constraints, not current runtime behavior.
