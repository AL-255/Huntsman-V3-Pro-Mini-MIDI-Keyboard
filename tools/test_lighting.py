#!/usr/bin/env python3
"""Check continuous PWM and channel routing against executed production rendering."""
import argparse
import ctypes as C
import random
from production_arm import ProductionArm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    lib = C.CDLL(args.library)
    lib.lighting_travel_pwm.argtypes = [C.c_uint16] * 3
    lib.lighting_travel_pwm.restype = C.c_uint8
    lib.lighting_travel_frame.argtypes = [C.c_uint8] + [C.POINTER(C.c_uint16)] * 3 + [C.c_bool, C.POINTER(C.c_uint8)]
    for lower, upper in ((1, 4096), (500, 3800), (2240, 3360), (1000, 1001)):
        previous = 255
        for raw in range(65536):
            expected = (0 if not 1 <= raw <= 4096 else 255 if raw <= lower else 0 if raw >= upper
                        else ((upper - raw) * 255 + (upper - lower) // 2) // (upper - lower))
            actual = lib.lighting_travel_pwm(raw, lower, upper)
            assert actual == expected, (raw, lower, upper, actual, expected)
            if lower <= raw <= upper:
                assert actual <= previous
                previous = actual
        assert lib.lighting_travel_pwm(lower, lower, upper) == 255
        assert lib.lighting_travel_pwm(upper, lower, upper) == 0
    for lo, hi in ((0, 4096), (500, 500), (501, 500), (500, 65535)):
        assert lib.lighting_travel_pwm(1000, lo, hi) == 0
    print('PASS 262144 uint16 PWM cases: monotonic, linear rounding, saturation, invalid fail-dark')

    rng = random.Random(0x98b0)
    for profile, count in ((1, 61), (2, 62), (3, 65)):
        p = ProductionArm(args.reference)
        p.write(0x200284fc, [profile])
        p.stubs[0x20015b34] = lambda *a: 0
        p.stubs[0x2000e3bc] = lambda *a: 0
        p.stubs[0x20008224] = lambda *a: 255
        p.stubs[0x2000a9d4] = lambda *a: 0
        p.call(0x2000cbf0)
        p.call(0x2000e410)
        grid = [p.read(0x20020800 + i*5, 5) for i in range(72)]
        keys = {r[profile + 1]: r[1] for r in grid if r[profile + 1] < count}
        matrix = [p.read(0x04000004 + i*9, 9) for i in range(75)]
        for trial in range(count + 12):
            raw = ([3800 if i != trial else 500 for i in range(count)] if trial < count
                   else [rng.randrange(1, 4097) for _ in range(count)])
            expected_levels = [255 if r <= 500 else 0 if r >= 3800
                               else ((3800 - r)*255 + 1650)//3300 for r in raw]
            inputs = bytearray(225)
            for i, key in keys.items():
                slot = next(j for j, row in enumerate(matrix) if row[0] == key)
                inputs[slot*3:slot*3 + 3] = bytes([expected_levels[i]]) * 3
            p.write(0x0400136c, inputs)
            p.write(0x2002b3a4, bytes(384))
            p.call(0x200098b0)  # actual production color->controller channel writes
            expected = p.read(0x2002b3a4, 204)
            output = (C.c_uint8 * 204)()
            lib.lighting_travel_frame(profile, (C.c_uint16 * count)(*raw),
                                      (C.c_uint16 * count)(*([500]*count)),
                                      (C.c_uint16 * count)(*([3800]*count)), True, output)
            assert bytes(output) == expected, (profile, trial)
            if trial < count:
                assert sum(v != 0 for v in output) == 3
        print(f'PASS layout {profile}: {count} single-key isolations and 12 mixed frames match original ARM renderer')
    output = (C.c_uint8 * 204)(*([255]*204))
    lib.lighting_travel_frame(1, None, None, None, False, output)
    assert not any(output)
    print('PASS invalid/unsettled frame clears every LED channel')


if __name__ == '__main__': main()
