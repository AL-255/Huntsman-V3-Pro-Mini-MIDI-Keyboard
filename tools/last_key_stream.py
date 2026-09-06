"""Strict HKL1 receiver: bounded output, no resampling or silent recovery."""
import os
import select
import struct
import time

SIZE = 20
ASSUMED_SCAN_HZ = 8000


def press_velocity(samples):
    """Five samples, four signed intervals, one discarded median outlier.

    The earliest interval wins equal-distance ties. Average the remaining
    three at the assumed 8 kHz; positive means press. Matches MCU filtering,
    before its 0..1 clamp/normalization; host output retains fractional counts/s.
    """
    if len(samples) != 5:
        raise ValueError('velocity requires exactly five readbacks')
    delta = [a-b for a,b in zip(samples,samples[1:])]
    ordered = sorted(delta)
    twice_median = ordered[1]+ordered[2]
    outlier = max(range(4),key=lambda i:abs(2*delta[i]-twice_median))
    return (sum(delta)-delta[outlier]) * ASSUMED_SCAN_HZ / 3


class StreamError(Exception):
    pass


class KeyDecoder:
    def __init__(self, threshold, session=None):
        self.threshold = threshold
        self.session = session
        self.buffer = bytearray()
        self.sequence = None
        self.key = 255
        self.prefix = 0

    def feed(self, data):
        self.buffer.extend(data)
        while True:
            if self.sequence is None:
                start = self.buffer.find(b'HKL1')
                skip = start if start >= 0 else max(0, len(self.buffer) - 3)
                self.prefix += skip
                del self.buffer[:skip]
                if self.prefix > 65536:
                    raise StreamError('HKL1 session not found; firmware may not support stream key')
            if len(self.buffer) < SIZE: return
            frame = bytes(self.buffer[:SIZE])
            if frame[:4] != b'HKL1':
                raise StreamError('lost HKL1 framing; refusing to skip data')
            session, seq, raw, key, flags, threshold, checksum = struct.unpack_from('<IIHBBHH', frame, 4)
            if self.sequence is None and self.session is not None and session != self.session:
                # Bytes queued before our nonce-bearing command belong to a
                # different capture; never confuse them with this session.
                del self.buffer[:1]
                self.prefix += 1
                continue
            if checksum != sum(struct.unpack('<9H', frame[:18])) & 65535:
                raise StreamError('HKL1 checksum failure')
            if self.sequence is not None and session != self.session:
                raise StreamError('unexpected HKL1 session change')
            if flags & ~7 or threshold != self.threshold:
                raise StreamError('unexpected HKL1 flags or threshold')
            if flags & 2:
                raise StreamError('device stream buffer overflow or USB data loss')
            if flags & 4:
                raise StreamError('invalid hardware readback or layout change')
            if self.sequence is None:
                if seq != 0 or not flags & 1:
                    raise StreamError('missing session-start report; data already lost')
            elif flags & 1 or seq != (self.sequence + 1) & 0xffffffff:
                raise StreamError('HKL1 sequence gap, duplicate, or unexpected session restart')
            if key != 255 and (key > 64 or not 1 <= raw <= 4096):
                raise StreamError('invalid key index or readback')
            if key == 255 and raw != 0:
                raise StreamError('invalid unselected-key report')
            self.sequence = seq
            self.session = session
            self.key = key
            del self.buffer[:SIZE]
            yield None if key == 255 else raw

    def finish(self):
        if self.sequence is None:
            raise StreamError('no HKL1 session received')
        if self.buffer:
            raise StreamError('truncated HKL1 report at end of input')


class KeyCapture:
    """Armed -> fixed-length capture -> wait for this key's release."""
    def __init__(self, threshold, count, labels, repeat=False):
        self.threshold, self.count, self.labels, self.repeat = threshold, count, labels, repeat
        self.reset()

    def reset(self):
        self.state = 'armed'
        self.key = None
        self.captured = 0
        self.first_five = []
        self.done = False

    def rearm(self, value):
        self.reset()
        return (f'Capture Armed: released raw={value}>{self.threshold}; '
                f'trigger=raw<{self.threshold}; next={self.count} (excluding trigger)\n')

    def feed(self, key, value):
        prefix = []
        if self.state != 'armed' and key != self.key:
            reason = (f'discarding incomplete capture ({self.captured}/{self.count} samples)'
                      if self.state == 'capture' else 'previous capture complete; release not observed')
            prefix.append(f'WARNING: selected sensor changed {self.key}->{key}; {reason}; starting over\n')
            self.reset()
            prefix.append(f'Capture Armed: restarting after key change; trigger=raw<{self.threshold}; '
                          f'next={self.count} (excluding trigger)\n')
        if value is None: return prefix
        if self.state == 'armed':
            if value >= self.threshold:
                return prefix  # wait for a below-threshold sample after release/restart
            if self.labels is None or key >= len(self.labels):
                raise StreamError('key index outside selected layout')
            self.key = key
            self.state = 'capture'
            return prefix + [f'Key: {self.labels[key]} (sensor {key})\n']
        if self.state == 'release':
            return [self.rearm(value)] if value > self.threshold else []
        self.captured += 1
        if len(self.first_five) < 5: self.first_five.append(value)
        lines = [f'{value}\n']
        if self.captured == self.count:
            if len(self.first_five) == 5:
                lines.append(f'Velocity: {press_velocity(self.first_five):+.3f} raw counts/s '
                             '(4 intervals, discard 1 outlier, average 3; assumed 8000 Hz; positive=press)\n')
            if self.repeat:
                self.state = 'release'
                if value > self.threshold: lines.append(self.rearm(value))
            else:
                self.done = True
        return lines


def receive(input_fd, output_fd, threshold=3800, buffer_frames=8192, timeout=5., session=None,
            sample_count=None, labels=None, repeat=False):
    """Drain all reports and emit each selected sample, failing if output lags.

    Nonblocking output is essential: a blocked pipe must not prevent overflow
    detection or leave an exception stranded in a background reader thread.
    """
    decoder = KeyDecoder(threshold, session)
    pending = bytearray()
    queued = 0
    eof = False
    last_report = time.monotonic()
    last_output = last_report
    capture = KeyCapture(threshold, sample_count, labels, repeat) if sample_count is not None else None

    def write_pending():
        nonlocal queued, last_output
        try:
            sent = os.write(output_fd, pending)
        except BlockingIOError:
            sent = 0
        queued -= pending[:sent].count(b'\n')
        del pending[:sent]
        if sent: last_output = time.monotonic()

    def enqueue(line):
        nonlocal queued, last_output
        if queued == buffer_frames:
            write_pending()
            if queued == buffer_frames:
                raise StreamError('host output buffer overflow; output cannot keep up')
        if not pending: last_output = time.monotonic()
        pending.extend(line.encode('ascii'))
        queued += 1
    old_input, old_output = os.get_blocking(input_fd), os.get_blocking(output_fd)
    try:
        os.set_blocking(input_fd, False)
        os.set_blocking(output_fd, False)
        while not eof or pending:
            readable, writable, _ = select.select(
                [] if eof else [input_fd], [output_fd] if pending else [], [], .05)
            # Drain an available sink first, without delaying input acquisition.
            if writable:
                write_pending()
            if readable:
                try:
                    data = os.read(input_fd, 65536)
                except BlockingIOError:
                    continue
                if not data:
                    if session is not None:
                        raise StreamError('device disconnected during capture')
                    decoder.finish()
                    if repeat:
                        raise StreamError('repeat capture input ended; expected continuous stream until Ctrl-C')
                    if sample_count is not None:
                        raise StreamError('capture ended before trigger and requested subsequent samples')
                    eof = True
                else:
                    for value in decoder.feed(data):
                        last_report = time.monotonic()
                        lines = capture.feed(decoder.key, value) if capture else ([] if value is None else [f'{value}\n'])
                        for line in lines: enqueue(line)
                        if capture and capture.done:
                            eof = True  # intentional boundary; do not consume later reports
                            break
            if not eof and time.monotonic() - last_report >= timeout:
                raise StreamError('readback timeout; no complete HKL1 reports received')
            if pending and time.monotonic() - last_output >= timeout:
                raise StreamError('output timeout; capture could not be printed')
    finally:
        os.set_blocking(input_fd, old_input)
        os.set_blocking(output_fd, old_output)
