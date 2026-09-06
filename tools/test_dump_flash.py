#!/usr/bin/env python3
"""Host-only dump protocol/PTY/verified-output checks; no device required."""
import contextlib
import io
import json
import os
from pathlib import Path
import pty
import select
import struct
import tempfile
import threading
import unittest
import zlib
from dump_flash import decode,dump


def packet(ident,address,error=False):
    data=bytearray(128); data[:4]=b'HBD1'
    struct.pack_into('<6I',data,4,ident,address,64,0x9de00,512,0)
    data[48:112]=bytes((address+i)%251 for i in range(64))
    if error:
        struct.pack_into('<I',data,36,116); data[64:80]=bytes(16)
    struct.pack_into('<I',data,124,zlib.crc32(data[:124]))
    return bytes(data)


class DumpTests(unittest.TestCase):
    def test_decode(self):
        p=packet(123,64,True)
        data,statuses,_,_=decode(p,123,64)
        self.assertEqual(statuses,(0,116,0,0)); self.assertEqual(data[16:32],bytes(16))
        for q,id_,addr in ((p,1,64),(p,123,0),(p[:-1],123,64),(p[:60]+b'X'+p[61:],123,64)):
            with self.assertRaises(ValueError): decode(q,id_,addr)

    def test_double_read_and_failure(self):
        for corrupt in (False,True):
            with self.subTest(corrupt=corrupt),tempfile.TemporaryDirectory() as directory:
                master,slave=pty.openpty(); path=os.ttyname(slave)
                stop=threading.Event(); calls=[]
                def peer():
                    pending=bytearray()
                    while not stop.is_set():
                        if not select.select([master],[],[],.01)[0]: continue
                        pending.extend(os.read(master,4096))
                        while b'\n' in pending:
                            line,_,pending=pending.partition(b'\n')
                            if not line.startswith(b'dump read '): continue
                            _,_,ident,address=line.split(); ident=int(ident); address=int(address)
                            calls.append(address); response=packet(ident,address,True)
                            if corrupt and len(calls)==3: response=response[:50]+b'Z'+response[51:]
                            for i in range(0,128,7): os.write(master,response[i:i+7])
                worker=threading.Thread(target=peer); worker.start()
                output=Path(directory)/'test.device-dump.bin'
                try:
                    with contextlib.redirect_stdout(io.StringIO()):
                        if corrupt:
                            with self.assertRaises(ValueError): dump(path,0,128,output)
                            self.assertFalse(output.exists())
                        else:
                            result=dump(path,0,128,output)
                            self.assertEqual(calls,[0,64,0,64])
                            self.assertEqual(result['verified_reads'],2)
                            self.assertEqual(result['errors'],[{'address':16,'status':116},{'address':80,'status':116}])
                            self.assertEqual(len(output.read_bytes()),128)
                            self.assertEqual(output.stat().st_mode&0o777,0o600)
                            self.assertEqual(json.loads(output.with_suffix('.bin.json').read_text()),result)
                            with self.assertRaises(FileExistsError): dump(path,0,128,output)
                finally:
                    stop.set(); worker.join(2); os.close(slave); os.close(master)


if __name__=='__main__': unittest.main()
