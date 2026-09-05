#!/usr/bin/env python3
"""Offline CDC stream tests. 8k/s is a synthetic source, not measured ASIC rate."""
import argparse
import struct
from decode_scan_stream import Decoder, decode
from test_keyboard_console_arm import ConsoleArm
from test_optical_bus_arm import ScanArm


def drain(dev):
    output = bytearray()
    for _ in range(128):
        dev.call('debug_service')
        if not dev.u32(dev.packet_entry(9)) & 0x80000000: break
        address, length = dev.packet(9)
        output.extend(dev.cpu.mem_read(address, length))
        dev.complete(9)
    else: raise AssertionError('CDC failed to drain')
    assert not dev.reset_requests
    return bytes(output)


def push(dev, index, profile=1):
    count = 65 if profile == 3 else 60 + profile
    values = [(index + i*17) & 65535 for i in range(count)]
    dev.cpu.mem_write(0x2003d000, struct.pack('<' + 'H'*count, *values))
    dev.call('scan_stream_push', 0x2003d000, count, profile, index)
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    for hs in (False, True):
        dev = ConsoleArm(args.elf, hs)
        dev.call('scan_stream_init')
        dev.call('scan_stream_start')
        decoder = Decoder()
        seen = 0
        total = 8000 if hs else 200
        for batch in range(0, total, 4):
            for i in range(batch, batch+4): push(dev, i, 3)
            # Synthetic host services four records per 500 us at HS. USB
            # packet/chunk completion executes actual vendor ARM code.
            data = drain(dev)
            for seq, tick, dropped, flags, values in decoder.feed(data):
                assert seq == seen and tick == seen and dropped == 0
                assert list(values) == [(seen+i*17) & 65535 for i in range(65)]
                assert flags == int(any(v == 0 or v > 4096 for v in values))
                seen += 1
        assert seen == total and not decoder.buffer
        print(f'PASS {"HS" if hs else "FS"} CDC: {total} complete uint16 records; checksum/split-packet reassembly')

    dev = ConsoleArm(args.elf, True)
    dev.call('scan_stream_init')
    dev.call('scan_stream_start')
    push(dev, 0)
    dev.call('debug_service')  # hold the host completion pending
    address, length = dev.packet(9)
    saved = bytes(dev.cpu.mem_read(address, length))
    for i in range(1, 101): push(dev, i)
    assert dev.call('scan_stream_dropped') == 68
    assert bytes(dev.cpu.mem_read(address, length)) == saved, 'in-flight packet overwritten'
    captured = drain(dev)
    records = list(Decoder().feed(captured))
    assert len(records) == 33 and [r[0] for r in records] == list(range(33))
    damaged = bytearray(captured[:160])
    damaged[42] ^= 1
    fragmented = b'old text' + damaged + captured
    decoder = Decoder()
    recovered = []
    for off in range(0, len(fragmented), 7):
        recovered.extend(decoder.feed(fragmented[off:off+7]))
    assert recovered == records, 'fragment/corruption resynchronization failed'
    push(dev, 101)
    result = list(Decoder().feed(drain(dev)))
    assert len(result) == 1 and result[0][:3] == (101, 101, 68)
    # While active, no text can contaminate frames.
    dev.cpu.mem_write(0x2003c000, b'NOT-IN-STREAM\0')
    dev.call('debug_write', 0x2003c000)
    assert drain(dev) == b''
    push(dev, 102)
    dev.call('debug_service')
    dev.call('scan_stream_stop')
    dev.cpu.mem_write(0x2003c000, b'TEXT-AFTER-STOP\0')
    dev.call('debug_write', 0x2003c000)
    data = drain(dev)
    assert decode(data[:160]) and data[160:] == b'TEXT-AFTER-STOP'
    print('PASS backpressure: whole-frame drops, sequence gaps, immutable pending IN, text arbitration')

    dev.call('scan_stream_start')
    push(dev, 103)
    dev.call('debug_service')
    dev.configure(True) # cancel pending IN; new USB session
    drain(dev)
    push(dev, 104)
    fresh = list(Decoder().feed(drain(dev)))
    assert len(fresh) == 1 and fresh[0][0] == 104
    print('PASS USB reset: cancelled packet not reused; fresh frames after reconfiguration')

    live = ScanArm(args.elf, args.reference)
    live.command('stream on')
    live.command('scan start')
    live.output.clear()
    live.service(350)
    records = list(Decoder().feed(live.output))
    assert records and all(r[4] == tuple(live.raw) for r in records)
    assert all(not any(r) for r in live.reports)
    output = live.command('stream off')
    assert b'SCAN phase=8' in live.command('scan status')
    print('PASS synthetic ASIC -> DMA -> raw CDC records, all sensors intact, host keys neutral')


if __name__ == '__main__': main()
