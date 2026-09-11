"""GUI telemetry, physical ANSI geometry and host profile validation."""
from dataclasses import dataclass
import struct
import math
import re
from scan_bars import sensor_labels

SIZE = 1152
MIDI_CONTROLS = {'Fn':'mode', 'RAl':'oct−', 'RCt':'oct+',
                 'LCt':'bend−', 'LAl':'bend+', 'LGu':'mod', 'Spc':'sustain'}
MAGICS = (b'HKG1',b'HKG2',b'HKG3',b'HKG4',b'HKG5',b'HKG6')


@dataclass(frozen=True)
class Snapshot:
    profile: int
    count: int
    flags: int
    result: int
    sequence: int
    revision: int
    ack: int
    scan_errors: int
    light_errors: int
    raw: tuple
    press: tuple
    release: tuple
    down: tuple
    report: bytes
    mode: int = 0
    velocity: tuple = ()
    captures: tuple = ()
    velocity_state: tuple = ()
    version: int = 1
    performance_mode: int = 0
    octave: int = 0
    midi_mapping: tuple = ()
    midi_cleanup: bool = False
    midi_errors: int = 0
    mode_changes: int = 0
    calibration_state: int = 0
    calibration_completed: int = 0
    calibration_selected: int = 255
    calibration_flags: int = 0
    calibration_hold: int = 0
    calibration_idle: int = 0
    calibration_done: tuple = ()
    calibration_reason: int = 0
    calibration_upper: int = 0
    calibration_lower: int = 0
    calibration_generation: int = 0
    calibration_error: int = 0


def decode(data):
    if len(data) not in (480,1088,SIZE) or data[:4] not in MAGICS:
        raise ValueError('bad GUI frame size/magic')
    size, version, profile, count, flags, result, mode = struct.unpack_from('<H6B',data,4)
    if (bytes(data[:4]),version,size) not in ((b'HKG1',1,480),(b'HKG2',2,1088),(b'HKG3',3,1088),(b'HKG4',4,SIZE),(b'HKG5',5,SIZE),(b'HKG6',6,SIZE)) or len(data) != size or mode > 2 or flags & ~63 or result > 2:
        raise ValueError('unsupported GUI header')
    if (profile,count) not in ((0,0),(1,61),(2,62),(3,65)):
        raise ValueError('invalid GUI layout')
    if sum(struct.unpack_from(f'<{(size-4)//2}H',data)) & 0xffffffff != struct.unpack_from('<I',data,size-4)[0]:
        raise ValueError('GUI checksum mismatch')
    padding = data[447:476] if version == 1 else data[1032:1084] if version < 4 else data[1101:1104]+(data[1112:1148] if version == 4 else data[1134:1136]+data[1144:1148])
    if any(padding) or data[430] & 0xfe:
        raise ValueError('invalid GUI padding')
    sequence,revision,ack,scan_errors,light_errors = struct.unpack_from('<5I',data,12)
    arrays = [struct.unpack_from('<65H',data,offset) for offset in (32,162,292)]
    if any(any(a[count:]) for a in arrays): raise ValueError('invalid sensor padding')
    raw,press,release = [a[:count] for a in arrays]
    if any(not 1 <= p < r < 4096 for p,r in zip(press,release)):
        raise ValueError('invalid threshold readback')
    if flags & 4 and any(not 1 <= v <= 4096 for v in raw):
        raise ValueError('valid flag contradicts raw values')
    bits = int.from_bytes(data[422:431],'little')
    if bits >> count: raise ValueError('invalid pressed bitmap')
    velocity = captures = states = ()
    if version >= 2:
        velocity = struct.unpack_from('<65f' if version >= 3 else '<65i',data,447)
        captures = struct.unpack_from('<65I',data,707)
        states = tuple(data[967:1032])
        if any(velocity[count:]) or any(captures[count:]) or any(states[count:]):
            raise ValueError('invalid velocity padding')
        invalid_values = any(not math.isfinite(v) or not 0.0 <= v <= 1.0 for v in velocity) if version >= 3 else any(abs(v) > 9828000 for v in velocity)
        if any(s & ~(15 if version >= 6 else 7) for s in states) or invalid_values:
            raise ValueError('invalid velocity data')
        velocity,captures,states = velocity[:count],captures[:count],states[:count]
    performance_mode = octave = errors = changes = 0
    mapping = (); cleanup = False
    if version >= 4:
        performance_mode,octave,channel,cleanup = struct.unpack_from('<BbBB',data,1032)
        mapping = tuple(data[1036:1036+count])
        if performance_mode > 1 or not -10 <= octave <= 10 or channel != 1 or cleanup > 1 or any(data[1036+count:1101]):
            raise ValueError('invalid MIDI state')
        if any(n > 127 and n != 255 for n in mapping): raise ValueError('invalid MIDI mapping')
        errors,changes = struct.unpack_from('<II',data,1104)
    cal = {}
    if version >= 5:
        state,completed,selected,cflags,hold,idle = struct.unpack_from('<4BHH',data,1112)
        done = int.from_bytes(data[1120:1129],'little')
        reason = data[1129]
        upper,lower = struct.unpack_from('<HH',data,1130)
        generation,error = struct.unpack_from('<II',data,1136)
        if (state > 8 or completed > count or selected != 255 and selected >= count or
            cflags & ~7 or bool(cflags & 1) != (1 <= state <= 5) or hold > 1000 or idle > 5000 or
            done >> count or done.bit_count() != completed or reason > 4 or upper > 4096 or lower > 4096):
            raise ValueError('invalid calibration state')
        if version >= 6 and any(v & 8 and (state != 3 or done & (1<<i)) for i,v in enumerate(states)):
            raise ValueError('invalid calibration hold bitmap')
        cal = dict(calibration_state=state,calibration_completed=completed,calibration_selected=selected,
                   calibration_flags=cflags,calibration_hold=hold,calibration_idle=idle,
                   calibration_done=tuple(bool(done & (1<<i)) for i in range(count)),calibration_reason=reason,
                   calibration_upper=upper,calibration_lower=lower,calibration_generation=generation,calibration_error=error)
    return Snapshot(profile,count,flags,result,sequence,revision,ack,scan_errors,light_errors,
                    raw,press,release,tuple(bool(bits & (1<<i)) for i in range(count)),bytes(data[431:447]),mode,
                    velocity,captures,states,version,performance_mode,octave,mapping,bool(cleanup),errors,changes,**cal)


class Decoder:
    def __init__(self):
        self.buffer = bytearray()
        self.started = False
        self.skipped = 0

    def feed(self,data):
        self.buffer.extend(data)
        if not self.started:
            starts = [i for magic in MAGICS if (i := self.buffer.find(magic)) >= 0]
            start = min(starts) if starts else -1
            skip = start if start >= 0 else max(0,len(self.buffer)-3)
            self.skipped += skip
            del self.buffer[:skip]
            if self.skipped > 65536: raise ValueError('GUI stream unavailable; install keyboard-gui firmware')
        while len(self.buffer) >= 6:
            if self.buffer[:4] not in MAGICS: raise ValueError('bad GUI frame magic')
            size = struct.unpack_from('<H',self.buffer,4)[0]
            if size not in (480,1088,SIZE): raise ValueError('bad GUI frame size')
            if len(self.buffer) < size: break
            result = decode(self.buffer[:size])
            del self.buffer[:size]
            self.started = True
            yield result


@dataclass(frozen=True)
class Key:
    sensor: int
    label: str
    x: float
    y: float
    width: float


def ansi_geometry():
    """Standard 15-unit ANSI 60% key sizes; sensor IDs come from recovered maps."""
    rows = [
        [('Esc',1)] + [(s,1) for s in '1234567890-='] + [('BkS',2)],
        [('Tab',1.5)] + [(s,1) for s in 'QWERTYUIOP[]'] + [('\\',1.5)],
        [('Cap',1.75)] + [(s,1) for s in "ASDFGHJKL;'"] + [('Ent',2.25)],
        [('LSh',2.25)] + [(s,1) for s in 'ZXCVBNM,./'] + [('RSh',2.75)],
        [('LCt',1.25),('LGu',1.25),('LAl',1.25),('Spc',6.25),
         ('Fn',1.25),('RAl',1.25),('Mnu',1.25),('RCt',1.25)],
    ]
    labels = sensor_labels()[61]
    keys = []
    for y,row in enumerate(rows):
        x = 0
        for label,width in row:
            keys.append(Key(labels.index(label),label,x,y,width))
            x += width
        assert x == 15
    assert sorted(k.sensor for k in keys) == list(range(61))
    return keys


def validate_pair(press,release):
    if type(press) is not int or type(release) is not int or not 1 <= press < release < 4096:
        raise ValueError('Require 1 <= press < release <= 4095 (press below; release above).')
    return press,release


def profile_from_snapshot(snapshot):
    if snapshot.profile != 1 or snapshot.count != 61:
        raise ValueError('This GUI supports the connected ANSI 61-key board only.')
    labels = sensor_labels()[61]
    return {'version':2 if snapshot.version >= 4 else 1,'layout':'ansi','keys':[
        {'sensor':i,'label':labels[i],'press':snapshot.press[i],'release':snapshot.release[i],
         **({'midi':snapshot.midi_mapping[i]} if snapshot.version >= 4 else {})}
        for i in range(61)]}


def validate_profile(data):
    if not isinstance(data,dict) or type(data.get('version')) is not int or data.get('version') not in (1,2) or data.get('layout') != 'ansi':
        raise ValueError('Expected an ANSI profile, version 1 or 2.')
    keys = data.get('keys')
    if not isinstance(keys,list) or len(keys) != 61: raise ValueError('Profile must contain all 61 keys.')
    labels = sensor_labels()[61]
    result = {}
    for key in keys:
        if not isinstance(key,dict): raise ValueError('Invalid profile key.')
        index = key.get('sensor')
        if type(index) is not int or not 0 <= index < 61 or index in result or key.get('label') != labels[index]:
            raise ValueError('Profile sensor/label mismatch or duplicate.')
        result[index] = validate_pair(key.get('press'),key.get('release'))
        if data['version'] == 2:
            note = key.get('midi')
            if type(note) is not int or not (0 <= note <= 127 or note == 255): raise ValueError('Invalid MIDI note.')
            if labels[index] in MIDI_CONTROLS and note != 255: raise ValueError('Reserved MIDI control key.')
    return result


def note_name(note):
    return 'Off' if note == 255 else f'{("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")[note%12]}{note//12-1}'


CAPTURE_POINTS = 20


class KeystrokeCapture:
    """Holds the first CAPTURE_POINTS samples of the latest keystroke.

    Armed until the watched key reports a down edge; the triggering sample
    becomes sample zero and every following sample appends one more. A new
    down edge always restarts the capture (latest keystroke wins), and the
    collected points are held — feed() returns False — until that happens.
    feed() consumes telemetry frames with the device-reported down state and
    velocity fit; feed_sample() consumes full-rate key-stream readbacks and
    applies the key's Schmitt pair itself, so samples 1..5 after the trigger
    match the device velocity window exactly (host-side fit stored in
    ``velocity``). The device fit counter attribution applies to telemetry
    frames: the fit whose completion counter first rises after the trigger
    belongs to this keystroke (normally already present in the triggering
    snapshot at the 33 ms telemetry throttle).
    """

    def __init__(self):
        self.reset()

    def reset(self):
        self.points = []
        self.prev_down = None  # None: adopt the next frame as baseline, never trigger
        self.fit = None      # (captures, velocity) of the keystroke's fit, if any
        self.velocity = None # host-computed raw counts/s (8 ksps captures)
        self.captures0 = None
        self.done = False
        self.armed = True

    def feed(self, raw, down, captures=None, velocity=None, fit_valid=False):
        """Consume one frame; return True when the waveform should refresh."""
        edge = down and self.prev_down is False
        self.prev_down = down
        if edge:
            self.points = [raw]
            self.captures0 = captures
            self.fit = None
            self.velocity = None
            self.done = False
            self.armed = False
            return True
        if self.done or self.armed:
            return False  # held or still waiting; the waveform stays frozen
        self.points.append(raw)
        if (self.fit is None and captures is not None and self.captures0 is not None
                and captures > self.captures0):
            self.fit = (captures, velocity)
        if len(self.points) >= CAPTURE_POINTS:
            if self.fit is None and fit_valid and captures is not None:
                self.fit = (captures, velocity)
            self.done = True
        return True

    def feed_sample(self, raw, press, release):
        """Consume one full-rate (8 ksps) key-stream sample.

        The down state uses the selected key's Schmitt pair; every sample is
        appended to an active capture so the trigger sample and the five
        following readbacks match the device velocity window exactly.
        """
        down = (raw < press) if not self.prev_down else (raw <= release)
        return self.feed(raw, down)


def parse_note(text):
    text = text.strip()
    if text.lower() in ('off','none','unmapped'): return 255
    if text.isdecimal():
        value = int(text)
    else:
        m = re.fullmatch(r'([A-Ga-g])([#b]?)(-?\d+)',text)
        if not m: raise ValueError('Use a MIDI number 0–127, note name (C0, Eb1, F#2), or Off.')
        letter,accidental,octave = m.groups()
        value = (int(octave)+1)*12 + {'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[letter.upper()] + {'':0,'#':1,'b':-1}[accidental]
    if not 0 <= value <= 127: raise ValueError('MIDI note must be 0–127 (C-1 through G9).')
    return value
