#!/usr/bin/env python3
"""Execute actual ARM keyboard/CDC/SDK paths, with synthetic ASIC and LED replies."""
import argparse
from keyboard_gui_model import Decoder
from test_keyboard_gui import packet
from test_keyboard_console_arm import ConsoleArm
from test_lighting_arm import LightingArm
from test_scan_stream_arm import drain, key_push
from last_key_stream import press_velocity, KeyDecoder
from scan_bars import sensor_labels
from lighting_reference_tables import recover


def midi_tests(args):
    dev = LightingArm(args.elf,args.reference)
    dev.service(400)
    labels = sensor_labels()[61]
    s = snapshot(dev,'stream gui')
    expected = {'Tab':72,'Q':74,'W':76,'E':77,'R':79,'T':81,'Y':83,'U':84,
                'I':86,'O':88,'P':89,'[':91,']':93,'\\':95,'1':73,'2':75,
                '4':78,'5':80,'6':82,'8':85,'9':87,'-':90,'=':92,'BkS':94,
                'LSh':60,'A':61,'Z':62,'S':63,'X':64,'C':65,'F':66,'V':67,
                'G':68,'B':69,'H':70,'N':71,'M':72,'K':73,',':74,'L':75,
                '.':76,'/':77,"'":78}
    assert s.performance_mode == 0
    assert s.midi_mapping == tuple(expected.get(label,255) for label in labels)
    def set_keys(**keys):
        for label,value in keys.items(): dev.raw[labels.index(label)] = value
        dev.service(10)
    set_keys(Fn=3500,Ent=3500)
    s=snapshot(dev); assert s.performance_mode == 1 and s.mode_changes == 1
    dev.service(200); assert snapshot(dev).mode_changes == 1
    set_keys(Fn=3900,Ent=3900); dev.service(30)
    dev.midi_packets.clear(); dev.reports.clear()
    values = dev.raw.copy(); values[labels.index('Tab')] = 3500
    one_raw_frame(dev,values)
    for v in (3400,3300,3200,3100,3000):
        values[labels.index('Tab')] = v; one_raw_frame(dev,values)
    dev.service(40)
    assert bytes([9,0x90,72,23]) in dev.midi_packets, dev.midi_packets
    assert any(p[:3] == bytes([10,0xa0,72]) for p in dev.midi_packets)
    assert all(not any(p) for p in dev.reports), dev.reports
    # The lower row's Shift key emits C4, with the pop filtered on the MCU.
    values=dev.raw.copy(); values[labels.index('LSh')]=3500
    one_raw_frame(dev,values)
    for v in (3400,3300,2300,2200,2100):
        values[labels.index('LSh')]=v; one_raw_frame(dev,values)
    dev.service(30)
    assert bytes([9,0x90,60,23]) in dev.midi_packets
    set_keys(LSh=3900)
    assert bytes([8,0x80,60,0]) in dev.midi_packets
    set_keys(LAl=3500); dev.service(40)
    assert snapshot(dev).octave == 1
    # Exercise the compiled overlay with actual recovered control-key routing.
    maps,_ = recover(args.reference)
    _,r,g,b = maps[0][labels.index('LAl')]
    _,cr,cg,cb = maps[0][labels.index('LCt')]
    for t,color in ((600000,(128,48,0)),(600600,(0,0,0))):
        dev.cpu.mem_write(0x2003d000,bytes([7])*204)
        dev.call('keyboard_midi_lights',dev.symbols['s_midi'],0x2003d000,t)
        rgb=bytes(dev.cpu.mem_read(0x2003d000,204))
        assert (rgb[r],rgb[g],rgb[b]) == color
        assert (rgb[cr],rgb[cg],rgb[cb]) == (7,7,7)
    set_keys(LAl=3900,Tab=3900)
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    s=snapshot(dev,'cfg midi 950 32 60'); assert s.result == 1 and s.midi_mapping[32] == 60
    dev.service(180)
    for cmd in ('cfg midi 951 32 128','cfg midi 951 99 60',f'cfg midi 951 {labels.index("Fn")} 60',
                'cfg midi 951 32 60 junk','cfg midi 951 32'):
        s=snapshot(dev,cmd); assert s.result == 2 and s.midi_mapping[32] == 60
    set_keys(A=3500); dev.service(30)
    assert any(p[:3] == bytes([9,0x90,72]) for p in dev.midi_packets)
    dev.call('keyboard_live_usb_reset'); dev.service(180)
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    assert bytes([11,0xb0,120,0]) in dev.midi_packets
    set_keys(A=3900); dev.service(10)
    set_keys(Fn=3500,Ent=3500); dev.service(180)
    assert snapshot(dev).performance_mode == 0
    set_keys(Fn=3900,Ent=3900); set_keys(A=3500)
    assert a(dev)
    print('PASS ARM USB MIDI: defaults, chord/hold, Note On velocity=23, poly aftertouch, octave-latched Off, GUI map ACK/reject, reset cleanup, HID isolation/recovery')


def snapshot(dev,command=None):
    dev.output.clear()
    if command: dev.command(command)
    dev.service(50)
    values = list(Decoder().feed(dev.output))
    assert values, bytes(dev.output)
    return values[-1]


def a(dev): return bool(dev.reports[-1][2] & 1)


def one_raw_frame(dev,values):
    dev.raw = list(values)
    before = sum(r[0] == 0xa0 for r in dev.requests)
    for _ in range(10):
        dev.service(1)
        if sum(r[0] == 0xa0 for r in dev.requests) > before: return
    raise AssertionError('Synthetic ASIC did not deliver a frame')


def velocity_tests(args):
    dev = LightingArm(args.elf,args.reference,3)  # exercise every supported sensor
    dev.service(400)
    snapshot(dev,'stream gui')
    s = snapshot(dev,'cfg enable 301 0')
    assert s.version == 6 and len(s.velocity) == 65 and not s.flags & 1
    one_raw_frame(dev,[3500]*65)
    for j in range(1,6): one_raw_frame(dev,[3500-(i+1)*j for i in range(65)])
    s = snapshot(dev)
    assert s.captures == (1,)*65, s.captures
    assert all(abs(v - 8000*(i+1)/4500000) < 1e-7 for i,v in enumerate(s.velocity)), s.velocity
    assert s.velocity_state == (2,)*65
    assert all(not any(r) for r in dev.reports)
    dev.raw[0] = 3700; s = snapshot(dev); assert not s.velocity_state[0] & 1
    dev.raw[0] = 3701; s = snapshot(dev); assert s.velocity_state[0] & 1
    rapid = [3500,3800,3490,3810,3480,3400,3390,3380,3370,3360]
    for value in rapid:
        values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
    s = snapshot(dev)
    assert s.captures[0] == 4 and s.captures[1:] == (1,)*64
    assert abs(s.velocity[0] - max(0,min(1,press_velocity(rapid[-5:])/4500000))) < 1e-7
    for points in ((2000,2100,2200,2300,2400),(3000,)*5,
                   (3000,2999,2998,2996,2976),(3500,2938,2376,1813,813),
                   (3500,2937,2374,1812,812),(3500,2700,1900,1100,300),
                   (3000,3000,2990,2970,2940),(3000,2970,2950,2940,2940),
                   (3400,3300,2300,2200,2100),(2000,1990,2990,2980,2970)):
        dev.raw[0] = 3900; baseline = snapshot(dev).captures[0]
        # Keep this single-strike fixture below release. A value of 4096 in
        # its window rearms and triggers a second valid capture, which can
        # replace the first result before the next slow GUI snapshot.
        values = dev.raw.copy(); values[0] = 3500; one_raw_frame(dev,values)
        for value in points:
            values[0] = value; one_raw_frame(dev,values)
        s = snapshot(dev)
        expected = max(0,min(1,press_velocity(points)/4500000))
        assert type(s.velocity[0]) is float and abs(s.velocity[0]-expected) < 1e-7, (points,s.velocity[0],expected,s.captures[0],baseline)
        assert s.captures[0] == baseline+1
    print('PASS ARM float32 pop filter: negative/zero clamp, fractional mean, below/above 4500000, high/low pops and tie handling')
    s = snapshot(dev,'cfg all 302 3100 3300')
    assert (s.ack,s.result,s.revision) == (302,1,1)
    assert s.press == (3100,)*65 and s.release == (3300,)*65
    assert not any(v & 6 for v in s.velocity_state)
    for bad in ('cfg all 303 3300 3100','cfg all 303 3000 4096',
                'cfg all 303 3000 3300 junk','cfg all 303 0 3300'):
        s = snapshot(dev,bad)
        assert (s.ack,s.result,s.revision) == (303,2,1)
        assert s.press == (3100,)*65 and s.release == (3300,)*65
    print('PASS ARM DMA -> 65 independent filtered velocities, overlapping retriggers, HID disabled, atomic all-key command/readback')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf'); parser.add_argument('--reference',required=True)
    args = parser.parse_args()
    midi_tests(args)
    velocity_tests(args)
    dev = LightingArm(args.elf,args.reference)
    # No CDC open: enumeration must be enough for scanning and keyboard output.
    dev.control_out(bytes.fromhex('21 22 00 00 04 00 00 00'))
    dev.service(400)
    assert b'RAW armed' not in dev.output
    dev.raw[32] = 3600; dev.service(10); assert not a(dev)
    dev.raw[32] = 3599; dev.service(10); assert a(dev)
    for value in (3600,3650,3700,3601,3699):
        dev.raw[32] = value; dev.service(10); assert a(dev)
    dev.raw[32] = 3701; dev.service(10); assert not a(dev)
    assert dev.transactions, 'lighting did not run alongside NKRO'
    dev.control_out(bytes.fromhex('21 22 01 00 04 00 00 00'))
    s = snapshot(dev,'stream gui')
    assert s.flags & 7 == 7 and s.count == 61 and not s.scan_errors and not s.light_errors
    dev.key(0x3b,True)  # FN
    dev.key(2,True)     # number 1 -> F1
    usage = 0x3a-4
    assert dev.reports[-1][2+usage//8] & (1 << (usage%8))
    s = snapshot(dev); assert s.flags & 32
    dev.key(2,False); dev.key(0x10,True)  # FN+Tab retained editor
    s = snapshot(dev); assert s.mode == 1 and not any(s.report)
    dev.key(0x10,False); dev.key(0x3b,False)
    s = snapshot(dev); assert s.mode == 1 and not s.flags & 32
    dev.key(0x6e,True); dev.key(0x6e,False)  # Escape exits
    s = snapshot(dev); assert s.mode == 0
    s = snapshot(dev,'cfg set 123 32 3000 3200')
    assert (s.ack,s.result,s.press[32],s.release[32],s.revision) == (123,1,3000,3200,1)
    for command in ('cfg set 124 32 3300 3200','cfg set 124 61 3000 3200',
                    'cfg set 124 32 0 3200','cfg set 124 32 3000 4097',
                    'cfg set 124 32 3000 3200 junk','cfg enable 124 2'):
        s = snapshot(dev,command)
        assert (s.ack,s.result,s.revision) == (124,2,1), command
    s = snapshot(dev,'cfg get 4294967295'); assert s.ack == 0xffffffff and s.result == 1
    s = snapshot(dev,'cfg get 4294967296'); assert s.ack == 0xffffffff
    dev.raw[32] = 3100; dev.service(10); assert not a(dev)
    dev.raw[32] = 2999; dev.service(10); assert a(dev)
    s = snapshot(dev); assert s.down[32] and s.report[2] & 1
    s = snapshot(dev,'cfg enable 150 0'); assert not s.flags & 3 and not a(dev)
    s = snapshot(dev,'cfg enable 151 1'); assert s.flags & 1 and not s.flags & 2 and not a(dev)
    dev.raw[32] = 3200; dev.service(10); assert not a(dev)
    dev.raw[32] = 3201; s = snapshot(dev); assert s.flags & 2
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Closing GUI / DTR is not a keyboard kill switch.
    dev.control_out(bytes.fromhex('21 22 00 00 04 00 00 00')); dev.service(20); assert a(dev)
    dev.raw[32] = 3900; dev.service(10); assert not a(dev)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Invalid sample releases immediately, held key cannot rearm.
    dev.raw[0] = 4097; dev.service(10); assert not a(dev)
    dev.raw[0] = 3900; dev.service(10); assert not a(dev)
    dev.raw[32] = 3900; dev.service(10)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Reset + reconfigure between main loop services still requires neutral.
    dev.reset(True)
    dev.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
    dev.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
    dev.service(20); assert not a(dev)
    dev.raw[32] = 3900; dev.service(10)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    dev.no_completion = True; dev.service(130); assert not a(dev)
    before = len(dev.requests); dev.service(100); assert len(dev.requests) == before
    assert not dev.reset_requests
    print('PASS ARM: auto NKRO without CDC; Schmitt boundaries; GUI ACK/readback; disable/neutral guards; reset/invalid/timeout release; LED concurrency')

    for hs in (False,True):
        dev = ConsoleArm(args.elf,hs)
        dev.call('scan_stream_init'); dev.call('scan_stream_gui')
        def push(sequence):
            dev.cpu.mem_write(0x2003d000,packet(sequence=sequence))
            dev.call('scan_stream_gui_push',0x2003d000)
        push(0); dev.call('debug_service')
        address,length = dev.packet(9); pending = bytes(dev.cpu.mem_read(address,length))
        for i in range(1,100): push(i)
        assert bytes(dev.cpu.mem_read(address,length)) == pending
        values = list(Decoder().feed(drain(dev)))
        assert [v.sequence for v in values] == [0,99]
        push(100); dev.call('debug_service')
        address,length = dev.packet(9); pending = bytes(dev.cpu.mem_read(address,length))
        dev.call('scan_stream_last_key',3600,77)
        key_push(dev,[3500]*61)
        assert bytes(dev.cpu.mem_read(address,length)) == pending
        assert list(KeyDecoder(3600,77).feed(drain(dev))) == [3500]
        key_push(dev,[3400]*61); dev.call('debug_service')
        dev.call('scan_stream_gui'); push(101)
        assert [v.sequence for v in Decoder().feed(drain(dev))] == [101]
        print(f'PASS {"HS" if hs else "FS"} GUI: stable pending transfer, latest-only replacement, 1152-byte framing')


if __name__ == '__main__': main()
