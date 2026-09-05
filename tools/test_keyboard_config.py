#!/usr/bin/env python3
"""Differential tests against actual production FN/editor ARM instructions.

Rendering, persistence, notification and calibration-rebuild boundaries are
stubbed. The mode decisions, FN handler, level selection and profile level
updates execute unchanged production instructions. No USB device is opened.
"""
import argparse
import ctypes as C
import random

from production_arm import ProductionArm


class State(C.Structure):
    _fields_ = [(name, C.c_uint8) for name in
                ('mode', 'fn', 'actuation', 'rapid', 'saved_actuation', 'saved_rapid',
                 'dirty', 'rapid_enabled', 'profile', 'locked')] + [('revision', C.c_uint32)]


class Reference(ProductionArm):
    def __init__(self, path, profile):
        super().__init__(path)
        self.write(0x200270ac, self.read(0x2002519c, 0x12d3))
        self.write(0x200284fc, [profile])
        self.write(0x04001802, [1])
        self.write(0x040008ec, [4])
        self.write(0x2002837f, [4])
        # No persistent flash, LEDs, USB notifications, or calibration model.
        for entry in (0x20017d9c, 0x2001676c, 0x20015b34, 0x2001620c,
                      0x2001a394, 0x2000be9c):
            self.stubs[entry] = lambda *args: 1
        if profile == 2:
            self.write(0x200275ed, self.read(0x2001b91c, 0x438))
        if profile == 3:
            self.write(0x200275ed, self.read(0x2001b4e4, 0x438))

    def event(self, key, down, fn):
        if key == 0x3b:
            self.call(0x200141f8, int(down))
        elif self.byte(0x04000901):
            self.call(0x200134fc, key, int(down), int(fn))
        else:
            action = self.call(0x2000b4d8, key, 0, int(fn))
            if action and self.byte(action + 1) == 0x11:
                self.call(0x2000f41c, action, int(down))

    def state(self):
        return tuple(self.byte(addr) for addr in
                     (0x04000901, 0x04000e42, 0x040008ec, 0x2002837f,
                      0x20027a25, 0x20027dde, 0x20026f90, 0x20027ddd))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    lib = C.CDLL(args.library)
    lib.keyboard_config_init.argtypes = [C.POINTER(State), C.c_uint8]
    lib.keyboard_config_event.argtypes = [C.POINTER(State), C.c_uint8, C.c_bool, C.c_bool]
    lib.keyboard_config_event.restype = C.c_bool
    lib.keyboard_key_for_sensor.argtypes = [C.c_uint8, C.c_uint8]
    lib.keyboard_key_for_sensor.restype = C.c_uint8
    rng = random.Random(0x134fc)
    total = 0
    for profile in (1, 2, 3):
        for locked in (0, 1):
            ref = Reference(args.reference, profile)
            state = State()
            lib.keyboard_config_init(C.byref(state), profile)
            state.locked = locked
            ref.write(0x200270af, [locked])
            # Deliberately includes release events, repeated presses, bounds,
            # editor switching, FN release before exit, and unrelated keys.
            scripted = [(0x3b, 1, 0), (0x10, 1, 1), (0x10, 0, 1),
                        (0x3b, 0, 0), (0x0b, 1, 0), (0x6e, 1, 0),
                        (0x3b, 1, 0), (0x1e, 1, 1), (0x1e, 0, 1),
                        (0x3b, 0, 0), (0x02, 1, 0), (0x1e, 1, 0),
                        (0x6e, 1, 0)]
            keys = [0x3b, 0x10, 0x1e, 0x6e, *range(2, 12), 0x19, 0x28,
                    0x26, 0x27, 0x39, 0x40, 0x3e, 0x81, 0x4f, 0x54, 0x53, 0x59, 0x1f]
            events = scripted + [(rng.choice(keys), rng.randrange(2), rng.randrange(2)) for _ in range(2000)]
            for index, (key, down, fn) in enumerate(events):
                ref.event(key, down, fn)
                lib.keyboard_config_event(C.byref(state), key, down, fn)
                actual = tuple(getattr(state, name) for name, _ in State._fields_[:8])
                assert actual == ref.state(), (profile, locked, index, hex(key), down, fn,
                                               actual, ref.state())
                total += 1
            print(f'PASS profile={profile}, locked={locked}: {len(events)} production editor events')
        # Compare active mapping, including the ANSI/ISO boot-time swap.
        ref = Reference(args.reference, profile)
        if profile != 3:
            ref.write(0x20020905, [0x3b])
            ref.write(0x2002090a, [0x3e])
        for sensor in range(65 if profile == 3 else 60 + profile):
            expected = ref.call(0x2000c1cc, sensor)
            assert lib.keyboard_key_for_sensor(profile, sensor) == expected, (profile, sensor, expected)
        assert lib.keyboard_key_for_sensor(profile, 255) == 0
        print(f'PASS profile={profile}: every sensor maps to the production key ID')
    print(f'PASS {total} differential mode/FN/level/commit observations')


if __name__ == '__main__':
    main()
