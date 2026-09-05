# Building and testing from a fresh checkout

## Prerequisites

The build uses a native C compiler for logic tests and Arm GNU bare-metal GCC
for the target. Tested target compiler: **14.2.1**. Put `arm-none-eabi-gcc`,
`arm-none-eabi-ar`, `arm-none-eabi-objcopy`, `arm-none-eabi-size` and their runtime
libraries on PATH. Also install CMake 3.21+, Ninja and Python 3.10+. The project
uses C11 and builds warnings as errors for application/logic code.

On Debian-family Linux, packages typically needed are `build-essential`,
`cmake`, `ninja-build`, `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`,
`python3`, `python3-tk` and `python3-venv`. Distribution compiler versions may
differ from the tested version. Review package installation normally; do not
put passwords or machine-specific credentials in build scripts.

The firmware uses unmodified, vendored official NXP SDK source subsets. Their
versions came from the MCUXpresso Installer 26.06.123 catalog installed at
`/home/yukidama/MCUXpressoInstaller` on the development machine. A fresh checkout
does **not** require that absolute path or reinstalling MCUXpresso: CMake uses
`third_party/nxp`, not files outside the repository. Exact revisions and licenses
are recorded in [ORIGINS](../third_party/ORIGINS.md).

## Fresh build

```sh
git clone git@github.com:AL-255/Huntsman-V3-Pro-Mini-MIDI-Keyboard.git
cd Huntsman-V3-Pro-Mini-MIDI-Keyboard
arm-none-eabi-gcc --version
cmake --version
ninja --version
python3 --version

cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests

cmake --preset keyboard-midi
cmake --build --preset keyboard-midi
```

The ARM toolchain file is `cmake/arm-none-eabi.cmake`; no IDE-generated project
is necessary. `keyboard-midi` inherits the recovered scan/lighting application
and enables standalone keyboard mode. `firmware` is a historical USB-only
bring-up preset and does **not** enable MIDI performance or automatic scanning.
Older preset names use the current source, not immutable historical revisions;
use git commits to reproduce source versions and preserve existing flashed
build directories when testing new changes.

Artifacts in `build-keyboard-midi`:

- `huntsman_firmware.elf`: debug symbols, linked ARM instructions and memory map.
- `huntsman_firmware.hex`: addressed Intel HEX.
- `huntsman_firmware.bin`: exactly 131072 bytes, with FF padding through the
  original updater-image boundary.

The linker reserves 127 KiB for code/initialized data and 1 KiB for future
application-owned configuration. These are RAM execution-image addresses, not
verified physical flash addresses. Post-build checks enforce the reservation,
blank new configuration slots, reset/stack bounds, USB VID/PID, interface order,
endpoint layout, strings and HID descriptors. They do not validate an actual
bootloader's flash mapping or authorize flashing.

## Tests that need no original firmware or device

The seven CTest suites cover core logic, raw keyboard/velocity, MIDI state,
GUI model/PTY transport, image reservation, scan display and compact captures.
Neither the updater EXE nor proprietary extracted firmware is needed for
these tests or the application build.

For linked-ARM USB tests:

```sh
python3 -m venv .venv-audit
. .venv-audit/bin/activate
python3 -m pip install -r tools/requirements-audit.txt
cmake --build --preset keyboard-midi --target audit-usb
```

Pinned optional dependencies are Unicorn 2.1.4 and pyelftools 0.33. The USB
target covers the compiled descriptor/control/endpoint paths at modeled full
and high speeds, Device-memory alignment, updater reset deferral, startup,
clock setup and USB chirp behavior. It does not open physical devices.

For a real Tk GUI test against a simulated serial peer, install Tk and `Xvfb`:

```sh
python3 -B tools/test_keyboard_gui_tk.py
```

The test starts a private virtual display with TCP disabled and uses POSIX PTYs,
not `/dev/ttyACM0`. Optional `--screenshot /tmp/gui.png` additionally needs Pillow.
The GUI runtime itself has no PySerial, Pillow or other pip dependency.

## Optional reference-backed audits

The original primary application is not distributed. If you already have a
lawfully obtained extraction, keep it read-only. The default reference is the
sibling path `../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin`.
To select another read-only location:

```sh
cmake --preset keyboard-midi \
  -DHUNTSMAN_PRODUCTION_REFERENCE=/absolute/path/to/primary-app.bin
cmake --build --preset keyboard-midi --target audit-keyboard
cmake --build --preset keyboard-midi --target audit-lighting
```

These execute compiled ARM scan/MIDI/LED paths using synthetic optical replies
and recovered reference tables, and check selected production-derived mappings.
Some tools pin the production hash; do not substitute another image silently.
The lighting target includes the keyboard audit. Running those targets without
the separate reference is expected to fail; it is not a build dependency.

## GUI access and troubleshooting

Run `python3 tools/keyboard_gui.py --demo` for a no-device preview. For hardware,
the user must have read/write permission on the device's CDC node. Use the
operating system's serial-access group/device permissions, and close other
monitors before connecting. The GUI takes an exclusive advisory lock and does
not steal a port from another owner. Its transport is Linux/POSIX-specific.

If telemetry rejects the new header, update the GUI to support HKG4. MIDI
mapping controls are disabled on older HKG1–HKG3 devices. If waiting for neutral,
release every key; inspect threshold/raw values without repeatedly resetting
the keyboard. A MIDI cleanup-pending indicator means the host has not yet
accepted all cleanup events. It does not prevent switching back to HID mode.

## Hardware updates are separate

No build/test target flashes or resets hardware. Use only the supplied updater's
reviewed application-only path after explicit authorization and record the exact
binary hash. The updater is a separate sibling project, not bundled here.
Do not overwrite bootloader, factory/calibration, stock settings or secondary
controller regions, and do not use manual forced bootloader recovery as a
routine test. The recorded single application update is complete; physical
MIDI/DAW playing validation remains outstanding. No further flash is implied.
