#!/usr/bin/env python3
"""Compare compiled chirp-workaround traces with the production ARM routine.

The optional production image is read-only and hash checked. Timer waits are
completed immediately with selected frame-number responses: this validates
branching/MMIO ordering, not physical PHY behavior or interrupt timing.
"""
import argparse
import hashlib
from pathlib import Path

from unicorn import UC_HOOK_MEM_WRITE

from test_usb_arm import UsbArm, USB

REFERENCE_SHA256 = "d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27"


class ChirpArm(UsbArm):
    def __init__(self, elf_path, advance_wait, reference=None):
        super().__init__(elf_path)
        self.events = []
        self.waits = []
        self.advance_wait = advance_wait
        self.counter = self.symbols["s_timer_ticks"]
        self.entry = "usb_errata_bus_reset"
        self.stubs.discard(self.symbols[self.entry] & ~1)
        if reference is not None:
            assert hashlib.sha256(reference).hexdigest() == REFERENCE_SHA256
            self.cpu.mem_write(0x20000000, reference)
            self.cpu.mem_write(0x04000000, bytes(0x8000))
            self.cpu.mem_write(0x20020000, bytes(0x10000))
            self.counter = 0x20028504
            self.entry = "production_chirp"
            self.symbols[self.entry] = 0x20005C85
            self.stubs.clear()
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.timer_write,
                          begin=0x40029004, end=0x40029007)

    def read_register(self, cpu, access, address, size, value, _):
        super().read_register(cpu, access, address, size, value, _)
        if address == USB + 4:
            self.events.append(("frame", self.registers.get(address, 0)))

    def write_register(self, cpu, access, address, size, value, _):
        if address == USB:
            self.events.append(("devcmd", value))
        super().write_register(cpu, access, address, size, value, _)

    def timer_write(self, cpu, access, address, size, value, _):
        if value & 1:
            count = self.u32(self.counter)
            self.waits.append(count)
            self.events.append(("wait", count))
            if len(self.waits) == self.advance_wait:
                self.registers[USB + 4] = 18
            self.put32(self.counter, 0)

    def run(self, speed):
        self.registers[USB] = (speed << 22) | 0x10010080
        self.registers[USB + 4] = 17
        self.call(self.entry)
        return self.events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("--reference", type=Path)
    args = parser.parse_args()
    reference = args.reference.read_bytes() if args.reference else None
    for speed, advance_wait, expected_waits in ((2, None, []), (1, None, [100, 1, 1]),
                                               (1, 1, [100, 1]), (1, 2, [100, 1, 1])):
        dev = ChirpArm(args.elf, advance_wait)
        events = dev.run(speed)
        assert dev.waits == expected_waits, (events, expected_waits)
        if reference is not None:
            original = ChirpArm(args.elf, advance_wait, reference)
            expected = original.run(speed)
            assert events == expected, (events, expected)
        if speed == 1:
            writes = [value for name, value in events if name == "devcmd"]
            assert len(writes) == (6 if advance_wait is None else 5), writes
            assert bool(writes[-1] & (1 << 21)) == (advance_wait is None)
            # Host-type latch must prevent repeated forced reconnect cycles.
            previous = list(dev.events)
            dev.call(dev.entry)
            assert dev.events == previous + [("frame", dev.registers[USB + 4])]
        print(f"PASS chirp speed={speed}, advancing wait={advance_wait}, waits={dev.waits}"
              + ("; exact production trace match" if reference is not None else ""))


if __name__ == "__main__":
    main()
