"""Single-owner POSIX CDC worker; acknowledged writes and latest-only telemetry."""
import fcntl
import os
import queue
import secrets
import select
import termios
import threading
import time
import tty
from keyboard_gui_model import Decoder


class Connection(threading.Thread):
    def __init__(self,path):
        super().__init__(daemon=True)
        self.path = path
        self.stop_event = threading.Event()
        self.requests = queue.Queue(maxsize=128)
        self.events = queue.Queue(maxsize=128)
        self.lock = threading.Lock()
        self.latest = None
        self.connected = False
        self.next_id = secrets.randbelow(0xfffffffe)+1

    def notify(self,text):
        try: self.events.put_nowait(text)
        except queue.Full: pass  # bounded informational log, never command/state data

    def submit(self,action,*args):
        self.requests.put_nowait((action,args))

    def snapshot(self):
        with self.lock: return self.latest

    def stop(self): self.stop_event.set()

    def run(self):
        fd = None
        original = None
        pending = None
        try:
            fd = os.open(self.path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            original = termios.tcgetattr(fd)
            tty.setraw(fd,termios.TCSANOW)
            decoder = Decoder()
            tx = bytearray(b'\nstream gui\n')
            pending = ('get',(),self.next_id)
            tx.extend(f'cfg get {self.next_id}\n'.encode())
            deadline = time.monotonic()+3
            last_rx = time.monotonic()
            while not self.stop_event.is_set():
                if pending is None and not tx:
                    try: action,args = self.requests.get_nowait()
                    except queue.Empty: pass
                    else:
                        self.next_id = self.next_id % 0xffffffff+1
                        pending = action,args,self.next_id
                        tx.extend(('cfg '+action+' '+str(self.next_id)+''.join(' '+str(v) for v in args)+'\n').encode())
                        deadline = time.monotonic()+3
                readable,writable,_ = select.select([fd],[fd] if tx else [],[],.02)
                if writable:
                    try: sent = os.write(fd,tx)
                    except BlockingIOError: sent = 0
                    del tx[:sent]
                if readable:
                    try: data = os.read(fd,65536)
                    except BlockingIOError: continue
                    if not data: raise OSError('CDC disconnected')
                    for snapshot in decoder.feed(data):
                        now = time.monotonic(); last_rx = now
                        with self.lock: self.latest = now,snapshot
                        if pending and snapshot.ack == pending[2]:
                            action,args,_ = pending
                            if snapshot.result != 1:
                                raise ValueError(f'Device rejected {action} {args}; remaining changes cancelled')
                            if action == 'set':
                                index,press,release = args
                                if index >= snapshot.count or (snapshot.press[index],snapshot.release[index]) != (press,release):
                                    raise ValueError('Threshold readback differs from requested values')
                            if action == 'all':
                                if not snapshot.count or any((p,r) != args for p,r in zip(snapshot.press,snapshot.release)):
                                    raise ValueError('All-key threshold readback differs from requested values')
                            if action == 'enable' and bool(snapshot.flags & 1) != bool(args[0]):
                                raise ValueError('Enable readback differs from requested state')
                            if action == 'midi':
                                index,note = args
                                if snapshot.version < 4 or index >= snapshot.count or snapshot.midi_mapping[index] != note:
                                    raise ValueError('MIDI mapping readback differs from requested values')
                            self.connected = True
                            self.notify(f'Confirmed {action} {args}' if action != 'get' else 'Connected: device telemetry acknowledged')
                            pending = None
                if pending and time.monotonic() >= deadline:
                    raise TimeoutError('Command not acknowledged; no retry. Remaining changes cancelled.')
                if time.monotonic()-last_rx > 2:
                    raise TimeoutError('Telemetry stale/disconnected. No further configuration sent.')
        except Exception as error:
            self.notify(f'ERROR: {error}')
        finally:
            self.connected = False
            if fd is not None:
                try:
                    if original is not None: termios.tcsetattr(fd,termios.TCSANOW,original)
                except OSError: pass
                os.close(fd)
            self.notify('Disconnected; keyboard operation does not depend on the GUI')
