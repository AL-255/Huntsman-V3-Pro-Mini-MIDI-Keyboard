#!/usr/bin/env python3
"""Offline CDC stream tests. 8k/s is a synthetic source, not measured ASIC rate."""
import argparse
import struct
from decode_scan_stream import Decoder, decode
from test_keyboard_console_arm import ConsoleArm
from test_optical_bus_arm import ScanArm
from last_key_stream import KeyDecoder, StreamError, SIZE as KEY_SIZE


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


def key_push(dev, values, profile=1):
    dev.cpu.mem_write(0x2003d000, struct.pack('<' + 'H'*len(values), *values))
    dev.call('scan_stream_push', 0x2003d000, len(values), profile, 0)


def compact_tests(elf):
    for hs in (False, True):
        dev = ConsoleArm(elf, hs)
        dev.call('scan_stream_init')
        dev.call('scan_stream_last_key', 3800, 123)
        raw = [3800]*61
        decoder = KeyDecoder(3800, 123)
        for changes, expected in (({},None), ({5:3799},3799), ({5:3900},3900),
                                  ({7:3500},3500), ({5:3700},3700),
                                  ({1:3600,2:3400},3600), ({1:3800},3800)):
            raw = raw.copy()
            for key,value in changes.items(): raw[key] = value
            key_push(dev,raw)
            assert list(decoder.feed(drain(dev))) == [expected]
        # Every sample is delivered at the synthetic target input rate; no
        # duplicate suppression, latest-only replacement, or hidden decimation.
        for batch in range(250 if hs else 10):
            for i in range(32): key_push(dev,raw)
            assert list(decoder.feed(drain(dev))) == [3800]*32
        decoder.finish()
        print(f'PASS {"HS" if hs else "FS"} compact: strict crossing/ties/release, every sample, packet reassembly')

    # Hold an old whole-keyboard transfer while changing mode. It must not be
    # overwritten or interpreted as this session's first compact report.
    dev = ConsoleArm(elf, True)
    dev.call('scan_stream_init'); dev.call('scan_stream_start')
    push(dev, 10); dev.call('debug_service')
    address,length = dev.packet(9)
    saved = bytes(dev.cpu.mem_read(address,length))
    dev.call('scan_stream_last_key', 3700, 456)
    key_push(dev,[3600]*61)
    assert bytes(dev.cpu.mem_read(address,length)) == saved
    data = drain(dev)
    assert data[:160] == saved
    assert list(KeyDecoder(3700,456).feed(data)) == [3600]
    dev.call('scan_stream_whole'); dev.call('scan_stream_start')
    push(dev, 11)
    assert len(list(Decoder().feed(drain(dev)))) == 1

    # Queue overflow must be latched and explicitly reported even if no new
    # input arrives after the overflow. It may not silently resume streaming.
    dev.call('scan_stream_last_key',3800,123)
    key_push(dev,[3700]*61); dev.call('debug_service')
    address,length = dev.packet(9)
    saved = bytes(dev.cpu.mem_read(address,length))
    for i in range(257): key_push(dev,[3700]*61)
    assert bytes(dev.cpu.mem_read(address,length)) == saved
    data = drain(dev)
    assert len(data) == 258*KEY_SIZE
    decoder = KeyDecoder(3800,123)
    assert len(list(decoder.feed(data[:-KEY_SIZE]))) == 257
    try: list(decoder.feed(data[-KEY_SIZE:])); raise AssertionError('overflow accepted')
    except StreamError as error: assert 'overflow' in str(error)
    key_push(dev,[3700]*61)
    assert drain(dev) == b''
    dev.call('scan_stream_last_key',3800,999)
    key_push(dev,[3700]*61)
    assert list(KeyDecoder(3800,999).feed(drain(dev))) == [3700]
    # Reset/cancel also fails the current compact session, never retries it.
    key_push(dev,[3700]*61); dev.call('debug_service')
    dev.configure(True)
    try: list(KeyDecoder(3800,999).feed(drain(dev))); raise AssertionError('reset accepted')
    except StreamError as error: assert 'overflow' in str(error)
    print('PASS compact mode switches, immutable pending IN, fail-stop overflow/reset, explicit restart')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    compact_tests(args.elf)
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
    # Exercise the actual CDC parser and hardware scan path, not just direct
    # calls to the stream producer. Fragmented commands are covered upstream.
    for bad in ('stream key 0', 'stream key 4097', 'stream key -1',
                'stream key 3800 4294967296', 'stream key 3800 junk', 'stream key 3800 '):
        assert b'ERR stream key' in live.command(bad)
    live.raw[4] = 3799
    data = live.command('stream key 3800 4294967295')
    live.output.clear(); live.service(30); data += bytes(live.output)
    values = list(KeyDecoder(3800,4294967295).feed(data))
    assert values and all(value == 3799 for value in values)
    assert all(not any(r) for r in live.reports)
    live.command('stream off')
    assert b'SCAN phase=8' in live.command('scan status')
    print('PASS CDC command/threshold/session parser -> real scan engine -> HKL1; neutral host keys')


if __name__ == '__main__': main()
