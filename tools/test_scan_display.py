#!/usr/bin/env python3
"""Host display tests, including a blocked output sink while input continues."""
import os
import io
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import unittest

from decode_scan_stream import Decoder, format_record, live_display
from scan_bars import BarDisplay, block, sensor_labels, compact_label


def plain(text):
    return re.sub(r'\x1b\[[?0-9;]*[A-Za-z]', '', text)


def packet(sequence):
    data = bytearray(160)
    struct.pack_into('<4sHBBIIIHH', data, 0, b'HKS1', 160, 61, 1,
                     sequence, sequence * 5, 0, 0, 0)
    struct.pack_into('<61H', data, 24, *([sequence] * 61))
    struct.pack_into('<I', data, 156, sum(struct.unpack_from('<78H', data)))
    return bytes(data)


class DisplayTests(unittest.TestCase):
    def test_recovered_labels(self):
        layouts = sensor_labels()
        for count in (61, 62, 65):
            self.assertEqual(len(layouts[count]), count)
            self.assertTrue(all(0 < len(label) <= 3 and '?' not in label for label in layouts[count]))
            self.assertEqual(layouts[count][:9], ['8', '7', '6', '5', '4', '3', '2', '1', 'Esc'])
            self.assertEqual(layouts[count][17], 'Tab')
        self.assertEqual(layouts[61][32:34], ['A', 'Cap'])
        self.assertEqual(layouts[61][42:44], ['RAl', 'Fn'])
        self.assertEqual(layouts[62][43:45], ['RAl', 'Fn'])
        self.assertEqual(layouts[65][43:45], ['Fn', 'RAl'])

    def test_block_endpoints_and_invalid(self):
        self.assertEqual(plain(block(1)), '▁ ')
        self.assertEqual(plain(block(4096)), '█ ')
        self.assertIn('\x1b[34m', block(1))
        self.assertIn('\x1b[31m', block(4096))
        self.assertEqual(plain(block(3585)), '█ ')
        self.assertEqual(plain(block(512)), '▁ ')
        self.assertEqual(plain(block(513)), '▂ ')
        for value in (0, 4097, 65535):
            self.assertEqual(plain(block(value)), '! ')
            self.assertIn('\x1b[35m', block(value))

    def test_caption_alignment_and_viewport(self):
        renderer = BarDisplay(io.StringIO(), columns=139)
        record = (1, 5, 0, 0, tuple(range(1, 62)))
        caption, values = renderer.lines(record)
        self.assertEqual(len(caption), 138)
        self.assertEqual(len(plain(values)), len(caption))
        self.assertEqual(caption[16 + 43 * 2:16 + 44 * 2], 'f ')
        self.assertEqual(plain(values)[16 + 43 * 2:16 + 44 * 2], '▁ ')
        renderer.columns = 80
        renderer.start = 40
        caption, values = renderer.lines(record)
        self.assertTrue(caption.startswith('40-60/61'))
        self.assertEqual(caption[16:18], 'Z ')
        self.assertEqual(len(caption), len(plain(values)))
        self.assertLess(len(caption), 80)
        renderer.columns = 10
        self.assertLess(len(renderer.lines(record)[0]), 10)

    def test_every_cell_is_one_character_then_space(self):
        for count, labels in sensor_labels().items():
            renderer = BarDisplay(io.StringIO(), columns=16 + count * 2 + 1)
            caption, values = renderer.lines((0, 0, 0, 0, (4096,) * count))
            keys, bars = caption[16:], plain(values)[16:]
            self.assertEqual(len(keys), count * 2)
            self.assertEqual(len(bars), count * 2)
            self.assertEqual(keys[1::2], ' ' * count)
            self.assertEqual(bars[1::2], ' ' * count)
            self.assertTrue(all(char.isascii() and not char.isspace() for char in keys[::2]))
            self.assertEqual(keys[::2], ''.join(compact_label(label) for label in labels))
            self.assertNotIn('?', keys)

    def test_in_place_two_rows_and_cleanup(self):
        output = io.StringIO()
        renderer = BarDisplay(output, columns=220)
        renderer((1, 5, 0, 0, (1,) * 61))
        renderer((2, 10, 0, 0, (4096,) * 61))
        renderer((3, 15, 0, 0, (2048,) * 65))
        captured = output.getvalue()
        # Model the renderer's cursor/erase controls, checking no scrollback
        # rows accumulate, including when a layout changes while connected.
        rows = [[], []]
        y = x = 0
        tokens = re.findall(r'\x1b\[[?0-9;]*[A-Za-z]|[^\x1b]', captured)
        for token in tokens:
            if token == '\x1b[1A':
                y -= 1
            elif token == '\x1b[2K':
                rows[y] = []
            elif token.startswith('\x1b'):
                continue
            elif token == '\r':
                x = 0
            elif token == '\n':
                y += 1
                self.assertLess(y, 2, 'display scrolled beyond its two rows')
            else:
                while len(rows[y]) <= x:
                    rows[y].append(' ')
                rows[y][x] = token
                x += 1
        expected = renderer.lines((3, 15, 0, 0, (2048,) * 65))
        self.assertEqual([''.join(row) for row in rows], [plain(line) for line in expected])
        renderer.close()
        self.assertTrue(output.getvalue().endswith('\x1b[0m\x1b[?7h\x1b[?25h\r\n'))
        length = len(output.getvalue())
        renderer.close()
        self.assertEqual(len(output.getvalue()), length)

    def test_bars_cli(self):
        result = subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('decode_scan_stream.py')),
                                 '--bars', '--duration', '.1'], input=packet(5), capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'keys |', result.stdout)
        self.assertIn('▁'.encode(), result.stdout)
        self.assertIn(b'\x1b[?25h', result.stdout)
        self.assertIn(b'received=1 printed=1', result.stderr)

    def test_decimal_width(self):
        a = (0, 1, 2, 0, (0, 1, 99, 4096, 65535))
        b = (4294967295, 4294967295, 4294967295, 1, (65535, 4096, 999, 10, 0))
        self.assertEqual(len(format_record(a)), len(format_record(b)))
        self.assertEqual([len(x) for x in format_record(a).split(',')], [10, 10, 10, 5] + [5] * 5)
        self.assertEqual(format_record(a).split(',')[-5:], ['    0', '    1', '   99', ' 4096', '65535'])

    def test_fragmentation_and_checksum(self):
        decoder = Decoder()
        bad = bytearray(packet(1))
        bad[24] ^= 1
        data = b'old text' + bytes(bad) + packet(2) + packet(3)
        records = []
        for start in range(0, len(data), 7):
            records.extend(decoder.feed(data[start:start + 7]))
        self.assertEqual([r[0] for r in records], [2, 3])

    def test_slow_output_drops_intermediate_reports(self):
        read_fd, write_fd = os.pipe()
        seen = []
        try:
            os.write(write_fd, packet(0))

            def slow_emit(record):
                seen.append(record[0])
                if len(seen) == 1:
                    # Input continues while the output callback is blocked.
                    # This is larger than a normal pipe capacity, so it also
                    # proves that the independent reader is still draining.
                    remaining = memoryview(b''.join(packet(i) for i in range(1, 101)))
                    while remaining:
                        remaining = remaining[os.write(write_fd, remaining):]
                    time.sleep(.1)

            with os.fdopen(read_fd, 'rb', buffering=0) as stream:
                received, printed = live_display(stream, slow_emit, rate=40, duration=.4)
            self.assertEqual(received, 101)
            self.assertEqual(printed, 2)
            self.assertEqual(seen, [0, 100])
        finally:
            os.close(write_fd)


if __name__ == '__main__':
    unittest.main()
