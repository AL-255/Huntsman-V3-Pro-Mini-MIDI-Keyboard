#!/usr/bin/env python3
"""Execute CDC OUT -> vendor ISR -> queue -> parser -> CDC IN on the ARM ELF.

Uses the existing limited USB register model. No connected device is touched.
"""
import argparse

from test_usb_arm import UsbArm


class ConsoleArm(UsbArm):
    def __init__(self, elf, high_speed):
        super().__init__(elf)
        self.console = self.symbols['s_console']
        self.call('debug_init')
        self.call('keyboard_console_init', self.console, self.symbols['debug_write'])
        self.call('usb_composite_init')
        self.configure(high_speed)

    def configure(self, high_speed):
        self.reset(high_speed)
        self.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
        self.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
        self.control_out(bytes.fromhex('21 22 01 00 04 00 00 00'))

    def drain(self):
        output = bytearray()
        for _ in range(100):
            self.call('debug_rx_service', self.console)
            self.call('debug_service')
            if self.u32(self.packet_entry(9)) & 0x80000000:
                address, length = self.packet(9)
                output.extend(self.cpu.mem_read(address, length))
                self.complete(9)
        assert self.reset_requests == 0
        assert not self.u32(self.packet_entry(3)) & 0x80000000, 'TEST generated a host keyboard transfer'
        return bytes(output)

    def command(self, text, fragmented=False):
        payload = text.encode('ascii') if isinstance(text, str) else text
        output = bytearray()
        while payload:
            _, length = self.packet(8)
            count = 1 if fragmented else min(length, len(payload))
            self.complete(8, payload[:count])
            payload = payload[count:]
            output.extend(self.drain())
        return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    args = parser.parse_args()
    for high_speed in (False, True):
        dev = ConsoleArm(args.elf, high_speed)
        assert b'optical=off' in dev.command('status\r\n', True)
        assert b'fn=1 mode=0' in dev.command('test key 3b down\n', True)
        assert b'fn=1 mode=1' in dev.command('test key 10 down\n')
        dev.command('test key 10 up\n')
        assert b'fn=0 mode=1' in dev.command('test key 3b up\n')
        assert b'act=10' in dev.command('test key 0b down\n')
        assert b'mode=0' in dev.command('test key 6e down\n')
        assert b'saved=10,4' in dev.command('test status\n')
        assert b'TEST report=' + b'0' * 32 in dev.command('test report\n')
        dev.command('test reset\n')
        dev.command('test key 3b down\n')
        assert b'mode=2' in dev.command('test key 1e down\n')
        dev.command('test key 1e up\ntest key 3b up\n')
        assert b'enabled=0' in dev.command('test key 1e down\n')
        assert b'ERR' in dev.command('test key ff down\n')
        assert b'ERR' in dev.command('test key 3b down trailing\n')
        assert b'ERR line discarded' in dev.command(b'x' * 80 + b'\n')
        assert b'ERR line discarded' in dev.command(b'test reset\x00\n')
        assert b'mode=2' in dev.command('test status\n'), 'malformed line changed state'
        # A partial line must not join bytes from the next USB connection.
        dev.command('test key 3b ')
        dev.configure(high_speed)
        assert b'ERR command' in dev.command('down\n')
        assert b'fn=0' in dev.command('test status\n')
        # Overflow must discard a suffix rather than execute a truncated command.
        for _ in range(20):
            dev.complete(8, b'x' * 64)
        dev.drain()
        assert b'ERR line discarded' in dev.command('test reset\n')
        assert b'mode=2' in dev.command('test status\n')
        assert b'mode=0' in dev.command('test reset\n')
        # Exercise ordinary NKRO packing in the isolated engine, not on the host.
        dev.command('test key 1f down\n')  # A
        assert b'TEST report=000001' in dev.command('test report\n')
        dev.command('test key 1f up\n')
        assert b'TEST report=' + b'0' * 32 in dev.command('test report\n')
        print(f'PASS {"HS" if high_speed else "FS"}: real CDC/ISR/parser/editor/debug path; '
              'fragmentation, overflow, bus-reset isolation, and no host key injection')


if __name__ == '__main__':
    main()
