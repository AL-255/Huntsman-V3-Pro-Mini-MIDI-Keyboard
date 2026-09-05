#!/usr/bin/env python3
"""Fail-closed checks for the application-only configuration allocation."""
import unittest

from validate_image import APP_BASE, APP_SIZE, CONFIG_OFFSET, validate_config_reservation


class ReservationTests(unittest.TestCase):
    def setUp(self):
        self.image = bytearray(b'\xff' * APP_SIZE)
        self.values = {
            '__app_config_start__': APP_BASE + CONFIG_OFFSET,
            '__app_config_slot_a__': APP_BASE + CONFIG_OFFSET,
            '__app_config_slot_b__': APP_BASE + CONFIG_OFFSET + 512,
            '__app_config_end__': APP_BASE + APP_SIZE,
            '__app_load_end__': APP_BASE + CONFIG_OFFSET,
        }

    def test_exact_boundary(self):
        validate_config_reservation(self.image, self.values)

    def test_missing_or_moved_symbols(self):
        for name in self.values:
            with self.subTest(name=name):
                missing = self.values.copy()
                del missing[name]
                with self.assertRaises(SystemExit):
                    validate_config_reservation(self.image, missing)
                moved = self.values.copy()
                moved[name] += 1
                with self.assertRaises(SystemExit):
                    validate_config_reservation(self.image, moved)

    def test_each_slot_must_be_blank(self):
        for offset in (CONFIG_OFFSET, CONFIG_OFFSET + 511,
                       CONFIG_OFFSET + 512, APP_SIZE - 1):
            with self.subTest(offset=offset):
                modified = self.image.copy()
                modified[offset] = 0
                with self.assertRaises(SystemExit):
                    validate_config_reservation(modified, self.values)

    def test_no_truncated_or_expanded_image(self):
        for image in (self.image[:-1], self.image + b'\xff'):
            with self.assertRaises(SystemExit):
                validate_config_reservation(image, self.values)


if __name__ == '__main__':
    unittest.main()
