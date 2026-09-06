#!/usr/bin/env python3
"""Read main flash via HBD1 CDC, twice; save private binary plus word-status map.

No reset, erase, program or automatic retry. Error words are ZERO PLACEHOLDERS,
not claimed readbacks; consult the JSON map. Never publish device dumps.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import struct
import termios
import time
import tty
import zlib

SIZE = 128
CHUNK = 64
LIMIT = 0x7f400


def decode(packet, request_id, address):
    if len(packet) != SIZE or packet[:4] != b'HBD1':
        raise ValueError('Invalid dump frame')
    if zlib.crc32(packet[:124]) != struct.unpack_from('<I',packet,124)[0]:
        raise ValueError('Dump CRC mismatch')
    ident,addr,length,total,page,status = struct.unpack_from('<6I',packet,4)
    if (ident,addr,length) != (request_id,address,CHUNK):
        raise ValueError('Stale, misaddressed or wrong-length dump response')
    if status:
        part,die=struct.unpack_from('<2I',packet,112)
        raise ValueError(f'Flash request failure: status {status}, size=0x{total:x}, part=0x{part:x}, die=0x{die:x}')
    if page != 512 or total < addr+CHUNK:
        raise ValueError('Unexpected flash geometry')
    statuses = struct.unpack_from('<4I',packet,32)
    data = packet[48:112]
    for i,status in enumerate(statuses):
        if status and any(data[i*16:i*16+16]):
            raise ValueError('Nonzero error placeholder')
    return data,statuses,total,page


def write_all(fd,data,timeout=3):
    deadline=time.monotonic()+timeout
    while data:
        remaining=deadline-time.monotonic()
        if remaining <= 0: raise TimeoutError('CDC write timed out; no retry')
        if select.select([], [fd], [], remaining)[1]:
            try: count=os.write(fd,data)
            except BlockingIOError: continue
            data=data[count:]


def read_frame(fd):
    data=bytearray()
    deadline=time.monotonic()+3
    while len(data)<SIZE:
        remaining=deadline-time.monotonic()
        if remaining <= 0: raise TimeoutError('Dump response timed out; no retry/reset')
        if select.select([fd],[],[],remaining)[0]:
            try: part=os.read(fd,SIZE-len(data))
            except BlockingIOError: continue
            if not part: raise OSError('CDC disconnected')
            data.extend(part)
    return bytes(data)


def capture(fd,start,length):
    data=bytearray(); statuses=[]; geometry=None
    request_id=secrets.randbelow(0xfffffffe)+1
    for address in range(start,start+length,CHUNK):
        request_id=request_id%0xffffffff+1
        write_all(fd,f'dump read {request_id} {address}\n'.encode())
        chunk,words,total,page=decode(read_frame(fd),request_id,address)
        if geometry is not None and geometry != (total,page):
            raise ValueError('Flash geometry changed during capture')
        geometry=(total,page)
        data.extend(chunk); statuses.extend(words)
        if len(data)%16384==0: print(f'Read {len(data)}/{length} bytes',flush=True)
    return bytes(data),statuses,geometry


def dump(device,start,length,output):
    output=Path(output).resolve()
    metadata=output.with_suffix(output.suffix+'.json')
    # Fail before accessing the device if either output already exists.
    if output.exists() or metadata.exists(): raise FileExistsError('Dump output already exists')
    output.parent.mkdir(parents=True,exist_ok=True)
    fd=os.open(device,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
    original=None; owned=False
    try:
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        # Also enforce against clients that don't use flock.
        fcntl.ioctl(fd,termios.TIOCEXCL)
        owned=True
        original=termios.tcgetattr(fd); tty.setraw(fd,termios.TCSANOW)
        write_all(fd,b'\nstream off\n')
        # Drain previously submitted scan/GUI/text data before requesting HBD1.
        deadline=time.monotonic()+2
        while select.select([fd],[],[],.15)[0]:
            if time.monotonic()>deadline: raise TimeoutError('CDC did not quiesce')
            if not os.read(fd,65536): raise OSError('CDC disconnected')
        print(f'First read: 0x{start:x}..0x{start+length:x}',flush=True)
        first=capture(fd,start,length)
        print('Independent second read for byte/status verification',flush=True)
        second=capture(fd,start,length)
        if first != second: raise ValueError('Independent reads differ; no dump saved')
    finally:
        if owned:
            try: write_all(fd,b'stream off\n',.5)
            except (OSError,TimeoutError): pass
            if original is not None:
                try: termios.tcsetattr(fd,termios.TCSANOW,original)
                except (OSError,termios.error): pass
            try: fcntl.ioctl(fd,termios.TIOCNXCL)
            except OSError: pass
        os.close(fd)
    data,statuses,(total,page)=first
    errors=[{'address':start+i*16,'status':s} for i,s in enumerate(statuses) if s]
    record={'format':'HBD1','address':start,'size':length,'flash_size':total,'page_size':page,
            'verified_reads':2,'sha256':hashlib.sha256(data).hexdigest(),
            'error_word_size':16,'error_placeholder':0,'errors':errors,
            'warning':'PRIVATE DEVICE READBACK. Failed words are placeholders, not original bytes.'}
    # Exclusive files: never clobber a previous backup. Permission 0600.
    for path,content in ((metadata,(json.dumps(record,indent=2)+'\n').encode()),(output,data)):
        fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as stream: stream.write(content)
    print(f'Saved {output}\nSHA256 {record["sha256"]}\nUnreadable 16-byte words: {len(errors)}; see {metadata}',flush=True)
    return record


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',default='/dev/ttyACM0')
    parser.add_argument('--start',type=lambda v:int(v,0),default=0)
    parser.add_argument('--length',type=lambda v:int(v,0),default=0x10000)
    parser.add_argument('--output',type=Path,required=True,help='Private output outside Git (or ignored device-dumps/)')
    args=parser.parse_args()
    if args.start<0 or args.length<=0 or (args.start|args.length)%CHUNK or args.start+args.length>LIMIT:
        parser.error('Range must be positive, 64-byte aligned, inside 0..0x7f400')
    try: dump(args.device,args.start,args.length,args.output)
    except (OSError,ValueError,TimeoutError) as error: parser.exit(1,f'ERROR: {error}\n')


if __name__=='__main__': main()
