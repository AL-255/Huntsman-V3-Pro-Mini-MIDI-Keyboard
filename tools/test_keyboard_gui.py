#!/usr/bin/env python3
"""Offline GUI model and real POSIX PTY transport tests (no keyboard access)."""
import copy
import os
import pty
import select
import struct
import threading
import time
import unittest
from keyboard_gui_model import SIZE, Decoder, decode, ansi_geometry, profile_from_snapshot, validate_profile, note_name, parse_note
from keyboard_gui_transport import Connection


def packet(ack=1, result=1, press=None, release=None, flags=7, sequence=0, version=4,
           velocity=None, captures=None, states=None, mapping=None, performance_mode=0, octave=0, calibration_state=0):
    size = SIZE if version >= 4 else 1088 if version >= 2 else 480
    data = bytearray(size)
    struct.pack_into('<4sH6B5I',data,0,f'HKG{version}'.encode(),size,version,1,61,flags,result,0,sequence,0,ack,0,0)
    for offset,values in ((32,[3900]*61),(162,press or [3600]*61),(292,release or [3700]*61)):
        struct.pack_into('<61H',data,offset,*values)
    if version >= 2:
        struct.pack_into('<61f' if version >= 3 else '<61i',data,447,*(velocity or [0]*61))
        struct.pack_into('<61I',data,707,*(captures or [0]*61))
        data[967:1028] = bytes(states or [1]*61)
    if version >= 4:
        struct.pack_into('<BbBB',data,1032,performance_mode,octave,1,0)
        data[1036:1097] = bytes(mapping or [255]*61)
    if version >= 5:
        data[1112]=calibration_state
        data[1114]=255; data[1115]=4 | int(1 <= calibration_state <= 5)
    struct.pack_into('<I',data,size-4,sum(struct.unpack_from(f'<{(size-4)//2}H',data)))
    return bytes(data)


class Device(threading.Thread):
    def __init__(self,fd,reject=False,mismatch=False,silent=False,version=4):
        super().__init__(daemon=True)
        self.fd,self.reject,self.mismatch,self.silent = fd,reject,mismatch,silent
        self.stop_event = threading.Event()
        self.commands = []
        self.press,self.release = [3600]*61,[3700]*61
        self.mapping = [255]*61
        self.flags,self.ack,self.result,self.sequence = 7,0,0,0
        self.error = None
        self.version=version; self.calibration_state=0

    def run(self):
        buffer = bytearray(); streaming = False; last = 0
        try:
            while not self.stop_event.is_set():
                if select.select([self.fd],[],[],.01)[0]:
                    buffer.extend(os.read(self.fd,4096))
                    while b'\n' in buffer:
                        line,_,buffer = buffer.partition(b'\n')
                        fields = line.decode().split()
                        if fields == ['stream','gui']: streaming = True
                        if not fields or fields[0] != 'cfg': continue
                        self.commands.append(fields)
                        self.ack = int(fields[2]); self.result = 1
                        if fields[1] == 'set':
                            index,press,release = map(int,fields[3:])
                            if self.reject: self.result = 2
                            elif not self.mismatch: self.press[index],self.release[index] = press,release
                        elif fields[1] == 'enable': self.flags = (self.flags & ~3) | int(fields[3])
                        elif fields[1] == 'all':
                            if self.reject: self.result = 2
                            elif not self.mismatch:
                                self.press = [int(fields[3])]*61; self.release = [int(fields[4])]*61
                        elif fields[1] == 'midi':
                            if self.reject: self.result = 2
                            elif not self.mismatch: self.mapping[int(fields[3])] = int(fields[4])
                        elif fields[1] == 'calibrate': self.calibration_state=3
                        elif fields[1] == 'calcancel': self.calibration_state=7
                if streaming and not self.silent and time.monotonic()-last > .03:
                    os.write(self.fd,packet(self.ack,self.result,self.press,self.release,self.flags,self.sequence,mapping=self.mapping,
                                           version=self.version,calibration_state=self.calibration_state,
                                           states=[9,9]+[1]*59 if self.version>=6 and self.calibration_state==3 else None))
                    self.sequence += 1; last = time.monotonic()
        except Exception as error: self.error = error


def until(predicate,seconds=3):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        if predicate(): return
        time.sleep(.01)
    raise AssertionError('Timed out waiting for test condition')


class Tests(unittest.TestCase):
    def test_calibration_model(self):
        s=decode(packet(version=5))
        self.assertEqual((s.version,s.calibration_state,s.calibration_flags,s.calibration_selected),(5,0,4,255))
        b=bytearray(packet(version=5))
        struct.pack_into('<4BHH',b,1112,3,1,32,5,500,4000)
        b[1120]=1
        struct.pack_into('<HH',b,1130,4000,1000)
        def checksum(data):
            struct.pack_into('<I',data,len(data)-4,sum(struct.unpack_from(f'<{(len(data)-4)//2}H',data)))
            return data
        s=decode(checksum(b)); self.assertTrue(s.calibration_done[0]); self.assertEqual(s.calibration_hold,500)
        for offset,value in ((1112,9),(1113,2),(1114,61),(1115,4),(1128,128),(1129,5),(1134,1),(1144,1)):
            bad=bytearray(b); bad[offset]=value
            with self.assertRaises(ValueError): decode(checksum(bad))
        parallel=bytearray(packet(version=6,calibration_state=3,states=[8,8]+[0]*59))
        s=decode(parallel); self.assertEqual(s.velocity_state[:3],(8,8,0))
        parallel[1112]=0; parallel[1115]=4
        with self.assertRaisesRegex(ValueError,'hold bitmap'): decode(checksum(parallel))
        legacy=packet(version=5,calibration_state=3,states=[8]+[0]*60)
        with self.assertRaisesRegex(ValueError,'velocity data'): decode(legacy)

    def test_midi_model(self):
        for n in range(128): self.assertEqual(parse_note(note_name(n)),n)
        self.assertEqual(parse_note('Eb0'),15)
        self.assertEqual(parse_note('C0'),12)
        self.assertEqual(parse_note('Off'),255)
        for bad in ('128','-1','C10','oops','C-2'):
            with self.assertRaises(ValueError): parse_note(bad)
        mapping = [255]*61; mapping[32]=60
        s=decode(packet(mapping=mapping,performance_mode=1,octave=-2))
        self.assertEqual((s.performance_mode,s.octave,s.midi_mapping[32]),(1,-2,60))
        p=profile_from_snapshot(s)
        self.assertEqual(p['version'],2)
        validate_profile(p)
        p['keys'][32]['midi']=128
        with self.assertRaises(ValueError): validate_profile(p)
        for offset,value in ((1032,2),(1033,11),(1034,2),(1035,2),(1036,128),(1101,1),(1112,1)):
            bad=bytearray(packet()); bad[offset]=value
            struct.pack_into('<I',bad,len(bad)-4,sum(struct.unpack_from(f'<{(len(bad)-4)//2}H',bad)))
            with self.assertRaises(ValueError): decode(bad)

    def test_midi_transport(self):
        resources=self.transport(); _,_,device,connection=resources
        try:
            until(lambda:connection.connected)
            connection.submit('midi',32,60)
            until(lambda:connection.snapshot()[1].midi_mapping[32]==60)
            self.assertEqual(device.commands[-1][1],'midi')
        finally: self.cleanup(*resources)
        resources=self.transport(mismatch=True); _,_,_,connection=resources
        try:
            until(lambda:connection.connected)
            connection.submit('midi',32,60)
            until(lambda:not connection.is_alive())
            self.assertTrue(any('MIDI mapping readback' in x for x in list(connection.events.queue)))
        finally: self.cleanup(*resources)

    def test_geometry(self):
        keys = ansi_geometry()
        self.assertEqual(sorted(k.sensor for k in keys),list(range(61)))
        self.assertEqual(next(k for k in keys if k.label == 'A').sensor,32)
        self.assertEqual(next(k for k in keys if k.label == 'Spc').width,6.25)
        self.assertEqual(next(k for k in keys if k.label == 'Fn').x,10)
        self.assertEqual(next(k for k in keys if k.label == 'RAl').x,11.25)
        for row in range(5):
            values = [k for k in keys if k.y == row]
            self.assertEqual(sum(k.width for k in values),15)
            for left,right in zip(values,values[1:]): self.assertEqual(left.x+left.width,right.x)

    def test_decoder(self):
        data = packet()
        d = Decoder(); results = []
        for byte in b'old CDC text\n'+data+data: results.extend(d.feed(bytes([byte])))
        self.assertEqual(len(results),2)
        self.assertEqual(results[0].raw,(3900,)*61)
        for index in (5,9,20,70,425,470,479):
            broken = bytearray(data); broken[index] ^= 1
            with self.assertRaises(ValueError): decode(broken)
        with self.assertRaises(ValueError): list(d.feed(b'x'+data))
        self.assertEqual(decode(packet(version=1)).version,1)
        self.assertEqual(decode(packet(version=1)).velocity,())
        s = decode(packet(version=2,velocity=[-123456]*61,captures=[45]*61,states=[7]*61))
        self.assertEqual(s.velocity,(-123456,)*61)
        self.assertEqual(s.captures,(45,)*61)
        self.assertEqual(s.velocity_state,(7,)*61)
        self.assertEqual([s.version for s in Decoder().feed(packet(version=1)+packet(version=2)+packet(version=3)+data)],[1,2,3,4])
        with self.assertRaises(ValueError): decode(packet(states=[8]*61))
        with self.assertRaises(ValueError): decode(packet(velocity=[9828001]*61))
        for value in (0.0,0.25,0.5,1.0):
            s = decode(packet(velocity=[value]*61,states=[2]*61))
            self.assertEqual(s.velocity,(value,)*61)
            self.assertIsInstance(s.velocity[0],float)
        for value in (-0.1,1.1,float('nan'),float('inf'),-float('inf')):
            with self.assertRaises(ValueError): decode(packet(velocity=[value]*61))

    def test_profiles(self):
        profile = profile_from_snapshot(decode(packet()))
        self.assertEqual(len(validate_profile(profile)),61)
        for field,value in (('sensor',61),('label','Wrong'),('press',3700),('press',True),('release',4097)):
            bad = copy.deepcopy(profile); bad['keys'][0][field] = value
            with self.assertRaises(ValueError): validate_profile(bad)
        bad = copy.deepcopy(profile); bad['keys'][0] = bad['keys'][1]
        with self.assertRaises(ValueError): validate_profile(bad)

    def transport(self,**options):
        master,slave = pty.openpty()
        device = Device(master,**options); connection = Connection(os.ttyname(slave))
        device.start(); connection.start()
        return master,slave,device,connection

    def cleanup(self,master,slave,device,connection):
        connection.stop(); connection.join(1)
        device.stop_event.set(); device.join(1)
        os.close(master); os.close(slave)
        self.assertFalse(connection.is_alive()); self.assertIsNone(device.error)

    def test_transport_ack_and_profile_batch(self):
        resources = self.transport(); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            connection.submit('enable',0)
            for i in range(61): connection.submit('set',i,3000+i,3300+i)
            connection.submit('enable',1)
            until(lambda:len(device.commands) == 64,5)
            until(lambda:connection.snapshot()[1].ack == int(device.commands[-1][2]))
            s = connection.snapshot()[1]
            self.assertEqual(s.press,tuple(range(3000,3061)))
            self.assertEqual(s.release,tuple(range(3300,3361)))
            self.assertTrue(s.flags & 1)
            connection.submit('all',3100,3400)
            until(lambda:connection.snapshot()[1].press == (3100,)*61)
            self.assertEqual(connection.snapshot()[1].release,(3400,)*61)
        finally: self.cleanup(*resources)

    def test_transport_rejection_and_readback_mismatch_cancel(self):
        for option in ('reject','mismatch'):
            for action,args in (('set',(32,3000,3200)),('all',(3000,3200))):
                resources = self.transport(**{option:True}); _,_,device,connection = resources
                try:
                    until(lambda:connection.connected)
                    connection.submit(action,*args)
                    connection.submit('enable',1)
                    until(lambda:not connection.is_alive())
                    self.assertEqual(len(device.commands),2)
                    self.assertFalse(connection.connected)
                finally: self.cleanup(*resources)

    def test_transport_stale_does_not_retry(self):
        resources = self.transport(silent=True); _,_,device,connection = resources
        try:
            until(lambda:not connection.is_alive(),4)
            self.assertEqual(len(device.commands),1)
            self.assertFalse(connection.connected)
        finally: self.cleanup(*resources)


if __name__ == '__main__': unittest.main()
