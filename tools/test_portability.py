#!/usr/bin/env python3
"""Keep hardware headers and address-space assumptions out of the application."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'firmware/app'

class PortabilityTests(unittest.TestCase):
    def test_includes_are_self_contained(self):
        standard={'stdbool.h','stdint.h','stddef.h','string.h'}
        for file in (*APP.glob('src/*.c'),*APP.glob('include/*.h')):
            for delimiter,name in re.findall(r'^\s*#include\s+([<"])([^>"]+)',file.read_text(),re.M):
                with self.subTest(file=file.name,header=name):
                    if delimiter=='<': self.assertIn(name,standard)
                    else: self.assertTrue((APP/'include'/name).is_file(),name)

    def test_no_hardware_dependencies_in_shared_code(self):
        forbidden=r'\b(?:KEY_ID_[A-Z_]+|g_lighting_channels|g_keyboard_grid|OPT_SCAN_READ|CAL_SLOT_[AB]|__WFI|__disable_irq|NVIC_SystemReset|USB_DeviceInit|FSL_[A-Z_]+)\b'
        for file in (*APP.glob('src/*.c'),*APP.glob('include/*.h')):
            source=re.sub(r'/\*.*?\*/|//[^\n]*','',file.read_text(),flags=re.S)
            self.assertIsNone(re.search(forbidden,source),str(file))

    def test_selectable_ports(self):
        cmake=(ROOT/'CMakeLists.txt').read_text()
        self.assertIn('MT_BOARD',cmake)
        self.assertNotIn('third_party/nxp',cmake)
        self.assertTrue((ROOT/'firmware/boards/synthetic/board.cmake').is_file())
        self.assertTrue((ROOT/'firmware/boards/huntsman_v3_pro_mini/board.cmake').is_file())

if __name__=='__main__': unittest.main()
