#!/usr/bin/env python3
"""Compare ordinary/rapid-trigger hysteresis against production ARM execution."""
import argparse
import ctypes as C
import random
import struct

from production_arm import ProductionArm


class State(C.Structure):
    _fields_ = [(x, C.c_uint8) for x in ('pressed', 'armed', 'trough', 'peak', 'cooldown')]


class Config(C.Structure):
    _fields_ = [(x, C.c_uint8) for x in
                ('press', 'release', 'rapid', 'continuous', 'release_delta', 'press_delta', 'wait_down', 'wait_up')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    lib = C.CDLL(args.library)
    lib.optical_key_level.argtypes = [C.c_uint16] * 3
    lib.optical_key_level.restype = C.c_uint8
    lib.optical_key_update.argtypes = [C.POINTER(State), C.POINTER(Config), C.c_uint8]
    lib.optical_key_update.restype = C.c_int
    rng = random.Random(0x1a918)
    ref = ProductionArm(args.reference)
    for lower, upper in ((2240, 3360), (620, 3800), (1, 4095), (0, 0), (3000, 1000)):
        for sample in range(4096):
            expected = ref.call(0x2000e354, lower, upper, sample, 0, 255)
            assert lib.optical_key_level(lower, upper, sample) == expected
    print('PASS 20,480 raw-to-level conversions against production')
    for rapid in (0, 1):
        for continuous in (0, 1):
            ref = ProductionArm(args.reference)
            row, col = 3, 1
            record = 0x20029ba8 + (row * 9 + col) * 39
            event_state = 0x20028513 + row * 0x36 + col * 6
            ref.write(0x200284fc, [1])
            ref.write(0x2002833f, [8])
            ref.write(0x20028341, [8])
            ref.write(record + 0x14, [rapid, 0, continuous, 25, 25, 0, 0])
            ref.write(record + 0x1f, struct.pack('<I', 68))
            ref.write(record + 0x23, struct.pack('<I', 76))
            events = []
            ref.stubs[0x2000d5d4] = lambda key, down, fn, _: events.append(1 if down else -1)
            state = State()
            config = Config(76, 68, rapid, continuous, 25, 25, 8, 8)
            levels = list(range(256)) + list(range(255, -1, -1)) + [rng.randrange(256) for _ in range(5000)]
            for index, level in enumerate(levels):
                events.clear()
                ref.call(0x2001a918, level, record, row, col)
                actual = lib.optical_key_update(C.byref(state), C.byref(config), level)
                expected = events[0] if events else 0
                expected_state = (ref.byte(event_state + 3), ref.byte(record + 0x15),
                                  ref.byte(record + 0x19), ref.byte(record + 0x1a), ref.byte(event_state))
                observed_state = tuple(getattr(state, name) for name, _ in State._fields_)
                assert actual == expected and observed_state == expected_state, (
                    rapid, continuous, index, level, actual, expected, observed_state, expected_state)
            print(f'PASS rapid={rapid}, continuous={continuous}: {len(levels)} production hysteresis steps')


if __name__ == '__main__':
    main()
