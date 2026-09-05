#!/usr/bin/env python3
"""Recover lighting maps and register programs by executing production ARM."""
import argparse
from pathlib import Path
from production_arm import ProductionArm


def recover(reference):
    maps = []
    for profile, count in ((1, 61), (2, 62), (3, 65)):
        p = ProductionArm(reference)
        p.write(0x200284fc, [profile])
        p.stubs[0x20015b34] = lambda *args: 0  # unrelated key threshold rebuild
        p.stubs[0x2000e3bc] = lambda *args: 0  # unrelated effect state setup
        p.call(0x2000cbf0)
        p.call(0x2000e410)
        matrix = [p.read(0x04000004 + 9*i, 9) for i in range(75)]
        result = [None] * count
        for pos in range(72):
            grid = p.read(0x20020800 + 5*pos, 5)
            sensor = grid[profile + 1]
            if sensor >= count:
                continue
            rows = [row for row in matrix if row[0] == grid[1]]
            assert len(rows) == 1, (profile, sensor, grid, rows)
            result[sensor] = tuple(rows[0][3:7])
        assert all(row is not None for row in result)
        maps.append(result)

    p = ProductionArm(reference)
    p.write(0x200284fc, [3])
    operations = []

    def write(address, reg, data, size):
        payload = p.read(data, size)
        assert payload == payload[:1] * size  # all recovered init writes are fills
        operations.append([address >> 1, reg, size, payload[0], 0])
        return 1

    def delay(ms, *_):
        operations[-1][4] += ms

    p.stubs[0x200096ec] = write
    p.stubs[0x200197d8] = delay
    programs = {}
    for name, entry, args in (('primary', 0x200092d0, (0, 0)),
                              ('secondary', 0x2000de70, (1, 0)),
                              ('maintenance', 0x200092d0, (0, 3))):
        operations = []
        assert p.call(entry, *args) == 1
        programs[name] = operations
    return maps, programs


def render(reference):
    maps, programs = recover(reference)
    out = ['/* Generated from executed, hash-pinned production ARM; see tools/lighting_reference_tables.py. */',
           '#include "travel_lighting.h"', '',
           'const lighting_channels_t g_lighting_channels[3][65] = {']
    for rows in maps:
        out.append('    {')
        for row in rows + [(255, 0, 0, 0)] * (65 - len(rows)):
            out.append('        {' + ', '.join(f'0x{x:02x}' for x in row) + '},')
        out.append('    },')
    out.extend(['};', ''])
    for name, rows in programs.items():
        out.append(f'const lighting_op_t g_lighting_{name}[] = {{')
        for row in rows:
            out.append('    {' + ', '.join(f'0x{x:02x}' for x in row) + '},')
        out.extend(['};', f'const unsigned g_lighting_{name}_count = {len(rows)}u;', ''])
    return '\n'.join(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    content = render(args.reference)
    if args.check:
        assert args.check.read_text() == content, 'lighting tables differ from production'
        print('PASS lighting sensor/channel maps, initialization and maintenance programs match production')
    else:
        print(content, end='')
