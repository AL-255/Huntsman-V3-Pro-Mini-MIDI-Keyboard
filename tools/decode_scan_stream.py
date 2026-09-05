#!/usr/bin/env python3
"""Decode HKS1 CDC binary capture from stdin or a file; no device commands.

Default output: sequence,tick,dropped,flags,raw0,... (unsigned decimal uint16).
Use --hex for four-digit hexadecimal readbacks, --summary for capture rates.
"""
import argparse
import struct
import sys

SIZE = 160


def decode(record):
    if len(record) != SIZE or record[:4] != b'HKS1': return None
    size, count, profile, sequence, tick, dropped, flags, reserved = struct.unpack_from('<HBBIIIHH', record, 4)
    if size != SIZE or profile not in (1, 2, 3) or count != (65 if profile == 3 else 60 + profile): return None
    if reserved or flags & ~1 or any(record[24 + count*2:156]): return None
    checksum = sum(struct.unpack_from('<78H', record)) & 0xffffffff
    if checksum != struct.unpack_from('<I', record, 156)[0]: return None
    return sequence, tick, dropped, flags, struct.unpack_from('<' + 'H'*count, record, 24)


class Decoder:
    def __init__(self): self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        while len(self.buffer) >= 4:
            start = self.buffer.find(b'HKS1')
            if start < 0:
                del self.buffer[:-3]
                return
            if start: del self.buffer[:start]
            if len(self.buffer) < SIZE: return
            result = decode(self.buffer[:SIZE])
            if result is None:
                del self.buffer[0]
                continue
            del self.buffer[:SIZE]
            yield result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', nargs='?', default='-')
    parser.add_argument('--hex', action='store_true')
    parser.add_argument('--summary', action='store_true')
    args = parser.parse_args()
    stream = sys.stdin.buffer if args.capture == '-' else open(args.capture, 'rb')
    decoder = Decoder()
    count = gaps = 0
    first = last = None
    while True:
        data = stream.read1(65536)
        if not data: break
        for result in decoder.feed(data):
            seq, tick, dropped, flags, samples = result
            if first is None: first = result
            if last is not None: gaps += ((seq - last[0]) & 0xffffffff) - 1
            last = result
            count += 1
            if not args.summary:
                values = [f'{v:04x}' if args.hex else str(v) for v in samples]
                print(','.join([str(seq), str(tick), str(dropped), str(flags), *values]))
    if args.summary and first is not None:
        ticks = (last[1] - first[1]) & 0xffffffff
        hz = (count - 1) * 8000 / ticks if ticks else 0
        print(f'frames={count} received_hz={hz:.2f} sequence_gaps={gaps} '
              f'dropped_counter={last[2]} elapsed_ticks={ticks}')


if __name__ == '__main__': main()
