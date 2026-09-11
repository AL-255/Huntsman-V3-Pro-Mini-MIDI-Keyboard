#!/usr/bin/env python3
"""Flash a built 128 KiB application image with the sibling updater project.

Uses the reviewed application-only path (`huntsman_updater.updater.update`):
enter the bootloader, stream the image over the channel-0x10 DFU interface and
wait for the device to re-enumerate in application mode. The bootloader,
factory/security data, primary settings and secondary-controller regions are
never written. Requires root for /dev/bus/usb access:

    printf 'PASSWORD\\n' | sudo -S -p '' python3 tools/flash_application.py \\
        build-keyboard-fn-menu/huntsman_firmware.bin

Record the printed sha256; the device is queried before and after the flash.
"""
import argparse
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, '/home/yukidama/Downloads/HuntsmanV3ProMini_02B0_FirmwareUpdater_v2.01.00_r1/updater/src')

from huntsman_updater import constants as C
from huntsman_updater import device, updater
from huntsman_updater.firmware import validate_app_image


class ApplicationPackage:
    """Raw application-only image; no secondary FlashFW content."""

    def __init__(self, image):
        self.pid = C.APP_PID
        self.bootloader_pid = C.BOOTLOADER_PID
        self.app_image = image
        self.flash_image = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='128 KiB raw application image (huntsman_firmware.bin)')
    parser.add_argument('--no-enter-boot', action='store_true',
                        help='device is already in the bootloader (1532:110E)')
    args = parser.parse_args()
    image = validate_app_image(Path(args.image).read_bytes())
    digest = hashlib.sha256(image).hexdigest()
    print(f'Application image: {len(image)} bytes, sha256 {digest}')
    try:
        print(f'Before flash: device version {device.query_version().hex(" ")}')
    except Exception as error:  # noqa: BLE001 - device may be absent/unreadable
        print(f'Before flash: version unavailable ({error})')

    def progress(done, total):
        print(f'\r  program {done}/{total} ({done*100//total}%)', end='', flush=True)
    updater.update(ApplicationPackage(image), enter_boot=not args.no_enter_boot,
                   flash_fw=False, progress=progress)
    print('\nFlash complete; device re-enumerated in application mode.')
    print(f'After flash: device version {device.query_version().hex(" ")}')
    print(f'SUCCESS: application flashed, sha256 {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
