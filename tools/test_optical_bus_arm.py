#!/usr/bin/env python3
"""Execute SDK SPI/DMA descriptors, scan scheduler and live CDC/NKRO on the ARM ELF.

ASIC replies and DMA completion are synthetic; this does not establish board
timing, electrical function, calibration validity or hardware recovery.
"""
import argparse
import struct
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from test_usb_startup_arm import StartupArm
from production_arm import ProductionArm

DMA = 0x40082000
SPI = 0x40089000


class ScanArm(StartupArm):
    def __init__(self, elf, reference, profile=1):
        super().__init__(elf)
        p = ProductionArm(reference)
        table = p.read(0x2001ddd6, 50)
        self.table = [struct.unpack('<BBBH', table[i:i+5]) for i in range(0, 50, 5)]
        self.profile = profile
        self.count = 65 if profile == 3 else 60 + profile
        self.raw = [3800] * self.count
        self.mode = None
        self.requests = []
        self.reports = []
        self.output = bytearray()
        self.dma_regs = {}
        self.no_completion = False
        self.bad_header = False
        self.marker = False
        self.cpu.hook_add(UC_HOOK_MEM_READ, self.dma_read, begin=DMA, end=DMA+0xfff)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.dma_write, begin=DMA, end=DMA+0xfff)
        self.call('board_init')
        self.put32(SPI + 0xff8, 0x20)  # modeled SPI-present read-only bit
        self.call('debug_init')
        self.call('keyboard_live_init')
        console = self.symbols['s_console']
        self.call('keyboard_console_init', console, self.symbols['debug_write'])
        self.put32(console + self.sizes['s_console'] - 4, self.symbols['keyboard_live_command'])
        self.call('usb_composite_init')
        self.reset(True)
        self.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
        self.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
        self.control_out(bytes.fromhex('21 22 01 00 04 00 00 00'))
        self.command('stream off') # These tests exercise human-readable command replies.

    def dma_read(self, cpu, access, address, size, value, _):
        self.put32(address, self.dma_regs.get(address, 0))

    def dma_write(self, cpu, access, address, size, value, _):
        offset = address - DMA
        if offset in (0x20, 0x48):
            value |= self.dma_regs.get(address, 0)
        elif offset in (0x28, 0x50):
            self.dma_regs[address-8] = self.dma_regs.get(address-8, 0) & ~value
        elif offset in (0x40, 0x58, 0x60):
            value = self.dma_regs.get(address, 0) & ~value
        self.dma_regs[address] = value

    def descriptor(self, channel):
        base = self.dma_regs[DMA + 8]
        assert base % 512 == 0 and 0x04000000 <= base < 0x04006000
        return struct.unpack('<4I', self.cpu.mem_read(base + 16*channel, 16))

    def dma_complete(self):
        if self.no_completion or not self.call('optical_bus_result') == 0:
            return
        if not self.dma_regs.get(DMA+8): return
        # Active transfer is indicated by SPI RXDMA enable, not by a stale descriptor.
        if not self.u32(SPI + 0xe00) & (1 << 13): return
        rx_cfg, rx_src, rx_end, rx_next = self.descriptor(8)
        tx_cfg, tx_end, tx_dst, tx_next = self.descriptor(9)
        length = (rx_cfg >> 16 & 0x3ff) + 1
        tx_length = (tx_cfg >> 16 & 0x3ff) + 1
        assert length in (2, 9, 12, 24, 67, 124, 126, 132, 197)
        assert rx_src == SPI+0xe30 and rx_next == 0
        assert tx_dst == SPI+0xe20 and tx_length == length-1 and tx_next % 16 == 0
        last_cfg, last_src, last_dst, last_next = struct.unpack('<4I', self.cpu.mem_read(tx_next, 16))
        last = self.u32(last_src)
        assert last_dst == SPI+0xe20 and last_next == 0 and last >> 16 & 0x10  # EOT
        assert last >> 24 & 15 == 7  # eight-bit SPI frame
        command = bytes(self.cpu.mem_read(tx_end-tx_length+1, tx_length)) + bytes([last & 255])
        self.requests.append(command)
        response = bytearray(length)
        response[:2] = b'\xc0\xa0'
        if command[0] == 0x30:
            assert command == bytes.fromhex('300000000400000067657476')
        elif command[0] == 0xb6:
            assert length == 2
            self.mode = command[1]
        elif command[0] == 0xa0:
            assert self.mode == 0 and length == self.count*2+2
            response[1] = 0xac if self.marker else 0xa0
            response[2:] = struct.pack('<' + 'H'*self.count, *self.raw)
        else:
            mode, request, reply, size = next(row for row in self.table if row[0] == self.mode)
            assert (command[0], length) == (request, size)
            assert not any(command[1:])
            response[1] = reply
            if mode == 2: response[7] = self.profile
            elif mode in (6, 8):
                for i in range(self.count):
                    struct.pack_into('<HB', response, 2 + i*3, 500 if mode == 6 else 3800, 0)
        if self.bad_header: response[0] = 0
        self.cpu.mem_write(rx_end-length+1, bytes(response))
        self.dma_regs[DMA + 0x58] = 1 << 8  # actual RX completion -> SDK callbacks
        self.call('DMA0_IRQHandler')
        assert self.call('optical_bus_result') == 1

    def service(self, count=1):
        for _ in range(count):
            self.call('SysTick_Handler')
            if self.u32(0x40028004) & 1:
                self.put32(0x40028000, 2)
                self.call('CTIMER2_IRQHandler')
            self.dma_complete()
            self.call('debug_rx_service', self.symbols['s_console'])
            self.call('keyboard_live_service')
            self.call('debug_service')
            if self.u32(self.packet_entry(3)) & 0x80000000:
                address, length = self.packet(3)
                self.reports.append(bytes(self.cpu.mem_read(address, length)))
                self.complete(3)
            if self.u32(self.packet_entry(9)) & 0x80000000:
                address, length = self.packet(9)
                self.output.extend(self.cpu.mem_read(address, length))
                self.complete(9)
            assert not self.reset_requests

    def command(self, text):
        self.output.clear()
        self.complete(8, text.encode() + b'\n')
        self.service(10)
        return bytes(self.output)

    def key(self, key, down):
        # Extracted map in the compiled application, tested independently
        # against the production mapper by test_keyboard_config.py.
        index = next(i for i in range(self.count)
                     if self.call('keyboard_key_for_sensor', self.profile, i) == key)
        self.raw[index] = 500 if down else 3800
        self.service(12)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    for profile in (1, 2, 3):
        dev = ScanArm(args.elf, args.reference, profile)
        assert b'phase=0' in dev.command('status')
        assert not dev.requests
        assert b'not armed' in dev.command('keys on')
        assert b'starting' in dev.command('scan start')
        dev.service(350)
        assert dev.u32(SPI + 0x424) == 11  # 96 MHz / (11+1) = 8 MHz
        assert dev.u32(SPI + 0x400) & 0x35 == 0x15  # enable/master/CPHA1/CPOL0
        assert dev.u32(0x4002801c) == 11999  # CTIMER2 MR1
        assert b'settled=1 valid=1' in dev.command('scan status')
        assert all(not any(report) for report in dev.reports), 'keys leaked before arming'
        dev.key(0x1f, True)
        assert b'not armed' in dev.command('keys on')
        dev.key(0x1f, False)
        assert b'host=1' in dev.command('keys on')
        dev.key(0x1f, True)
        assert dev.reports[-1][2] & 1
        dev.key(0x1f, False)
        assert not any(dev.reports[-1])
        dev.key(0x3b, True)
        dev.key(0x10, True)
        assert b'mode=1' in dev.command('keys status')
        dev.key(0x10, False)
        dev.key(0x3b, False)
        dev.key(0x0b, True)
        dev.key(0x0b, False)
        dev.key(0x6e, True)
        dev.key(0x6e, False)
        assert b'saved=10,4' in dev.command('keys status')
        dev.key(0x3b, True)
        dev.key(0x1e, True)
        assert b'mode=2' in dev.command('keys status')
        dev.key(0x1e, False)
        dev.key(0x3b, False)
        dev.key(0x6e, True)
        dev.key(0x6e, False)
        dev.key(0x1f, True)
        dev.no_completion = True
        dev.service(30)
        assert b'fault=SPI timeout' in dev.command('scan status')
        assert not any(dev.reports[-1])
        before = len(dev.requests)
        assert b'already attempted' in dev.command('scan start')
        dev.service(100)
        assert len(dev.requests) == before
        assert b'TEST' in dev.command('test status'), 'CDC unavailable after scan fault'
        print(f'PASS layout={profile}: SDK DMA descriptors/IRQ -> ASIC scheduler -> physical engine -> NKRO; '
              'CDC editors, arming, timeout releases, no retry/reset')

    for failure in ('header', 'ready', 'marker', 'invalid', 'reset', 'stop'):
        dev = ScanArm(args.elf, args.reference)
        dev.command('scan start')
        dev.service(350)
        dev.command('keys on')
        dev.key(0x1f, True)
        assert any(dev.reports[-1])
        if failure == 'header': dev.bad_header = True
        elif failure == 'ready': dev.cpu.mem_write(0x4008c013, b'\1')
        elif failure == 'marker': dev.marker = True
        elif failure == 'invalid': dev.raw[0] = 4097
        elif failure == 'stop': dev.command('scan stop')
        else:
            # Reset and reconfigure/DTR before main: the reset latch must
            # still disarm, even if main never observes DTR low.
            dev.reset(True)
            dev.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
            dev.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
            dev.control_out(bytes.fromhex('21 22 01 00 04 00 00 00'))
        dev.service(150)
        assert not any(dev.reports[-1]), failure
        assert b'host=0' in dev.command('keys status'), failure
        assert b'TEST' in dev.command('test status'), failure
        print(f'PASS failure={failure}: neutral host report, CDC remains responsive, no reset intent')


if __name__ == '__main__':
    main()
