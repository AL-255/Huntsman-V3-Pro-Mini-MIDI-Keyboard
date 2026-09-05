#!/usr/bin/env python3
"""Standalone ANSI keyboard monitor and per-key Schmitt configuration GUI."""
import argparse
from collections import deque
import json
import math
from pathlib import Path
import queue
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from keyboard_gui_model import Snapshot, ansi_geometry, profile_from_snapshot, validate_pair, validate_profile, note_name, parse_note
from keyboard_gui_transport import Connection


class App:
    def __init__(self,root,device='/dev/ttyACM0',demo=False):
        self.root,self.demo = root,demo
        self.connection = None
        self.snapshot = None
        self.selected = 32
        self.keys = ansi_geometry()
        self.items = {}
        self.titles = {}
        self.history = deque(maxlen=180)
        self.last_sequence = None
        self.initial_fields = False
        root.title('Huntsman • Keyboard configuration')
        root.geometry('1180x840'); root.minsize(930,820)
        root.configure(bg='#101820')
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TFrame',background='#101820')
        style.configure('TLabel',background='#101820',foreground='#d9e5ec')
        style.configure('TButton',padding=7)
        style.configure('Title.TLabel',font=('sans',18,'bold'))
        outer = ttk.Frame(root,padding=18); outer.pack(fill='both',expand=True)
        ttk.Label(outer,text='HUNTSMAN  /  KEYBOARD',style='Title.TLabel').pack(anchor='w')
        ttk.Label(outer,text='Raw Schmitt thresholds • press below the lower value, release above the upper value').pack(anchor='w',pady=(3,12))
        bar = ttk.Frame(outer); bar.pack(fill='x')
        self.device = tk.StringVar(value=device)
        ttk.Entry(bar,textvariable=self.device,width=25).pack(side='left')
        self.connect_button = ttk.Button(bar,text='Connect',command=self.toggle_connection)
        self.connect_button.pack(side='left',padx=6)
        self.enable_button = ttk.Button(bar,text='Enable keyboard',command=lambda:self.enable(True))
        self.enable_button.pack(side='left',padx=3)
        self.disable_button = ttk.Button(bar,text='Disable keyboard',command=lambda:self.enable(False))
        self.disable_button.pack(side='left',padx=3)
        ttk.Button(bar,text='Save profile…',command=self.save_profile).pack(side='right',padx=3)
        self.load_button = ttk.Button(bar,text='Load + apply profile…',command=self.load_profile)
        self.load_button.pack(side='right',padx=3)
        self.status = tk.StringVar(value='DEMO — no device access' if demo else 'Disconnected — connect to keyboard-gui firmware')
        ttk.Label(outer,textvariable=self.status,wraplength=1100).pack(anchor='w',pady=(12,4))
        self.canvas = tk.Canvas(outer,height=270,bg='#101820',highlightthickness=0)
        self.canvas.pack(fill='x'); self.canvas.bind('<Configure>',lambda _:self.draw())
        ttk.Label(outer,text='Orange = sensor down   •   Cyan = selected   •   Numbers = raw / device velocity (0–1; legacy firmware: counts/s)').pack(anchor='w',pady=(0,12))
        self.message = tk.StringVar(value='No flash/reset operations are performed by this GUI.')
        self.footer = ttk.Label(outer,textvariable=self.message,wraplength=890)
        self.footer.pack(side='bottom',anchor='w',pady=(12,0))
        lower = ttk.Frame(outer); lower.pack(fill='both',expand=True)
        panel = ttk.Frame(lower); panel.pack(side='left',fill='y',padx=(0,20))
        self.key_title = tk.StringVar(value='A  /  sensor 32')
        ttk.Label(panel,textvariable=self.key_title,style='Title.TLabel').pack(anchor='w')
        self.details = tk.StringVar(value='Waiting for device telemetry')
        ttk.Label(panel,textvariable=self.details,justify='left',wraplength=355).pack(anchor='w',pady=10)
        self.press = tk.StringVar(value='3600'); self.release = tk.StringVar(value='3700')
        for title,var in (('Press when raw <',self.press),('Release when raw >',self.release)):
            row = ttk.Frame(panel); row.pack(fill='x',pady=3)
            ttk.Label(row,text=title,width=21).pack(side='left')
            ttk.Entry(row,textvariable=var,width=9).pack(side='left')
        buttons = ttk.Frame(panel); buttons.pack(anchor='w',pady=9)
        self.apply_button = ttk.Button(buttons,text='Apply to selected key',command=self.apply)
        self.apply_button.pack(side='left')
        self.apply_all_button = ttk.Button(buttons,text='Apply thresholds to all keys',command=self.apply_all)
        self.apply_all_button.pack(side='left',padx=(6,0))
        midi_row = ttk.Frame(panel); midi_row.pack(anchor='w',pady=3)
        ttk.Label(midi_row,text='MIDI note: ').pack(side='left')
        self.midi_note = tk.StringVar(value='Off')
        self.midi_entry = ttk.Combobox(midi_row,textvariable=self.midi_note,width=9,
                                      values=['Off']+[note_name(n) for n in range(128)])
        self.midi_entry.pack(side='left')
        self.midi_button = ttk.Button(midi_row,text='Apply MIDI mapping',command=self.apply_midi)
        self.midi_button.pack(side='left',padx=6)
        ttk.Label(panel,text='Fn+Enter: keyboard ↔ MIDI; LCtrl/LAlt: octave −/+\nMIDI channel 1; C0=12; C4=60. Notes/Off configurable.\nRAM-only; host JSON export includes MIDI mappings.\nConfig edits release keys/notes and wait for neutral.',justify='left').pack(anchor='w')
        self.graph = tk.Canvas(lower,height=200,bg='#17232d',highlightthickness=0)
        self.graph.pack(side='right',fill='both',expand=True)
        root.protocol('WM_DELETE_WINDOW',self.close)
        if demo: self.connect_button.configure(state='disabled')
        self.update()

    def draw(self):
        self.canvas.delete('all'); self.items.clear(); self.titles.clear()
        unit = max(50,(self.canvas.winfo_width()-4)/15)
        height = 52
        for key in self.keys:
            x,y = key.x*unit+2,key.y*height+2
            tag = f'key{key.sensor}'
            rect = self.canvas.create_rectangle(x,y,x+key.width*unit-4,y+height-5,
                                                fill='#21313e',outline='#354958',width=2,tags=tag)
            self.titles[key.sensor] = self.canvas.create_text(x+key.width*unit/2-2,y+12,text=key.label,fill='#f0f5f7',font=('sans',10,'bold'),tags=tag)
            text = self.canvas.create_text(x+key.width*unit/2-2,y+28,text='—',fill='#9cafbc',font=('monospace',10),tags=tag)
            velocity = self.canvas.create_text(x+key.width*unit/2-2,y+40,text='v —',fill='#80c8ce',font=('monospace',8),tags=tag)
            self.items[key.sensor] = rect,text,velocity
            self.canvas.tag_bind(tag,'<Button-1>',lambda _,i=key.sensor:self.select(i))
        self.paint()

    def select(self,index):
        self.selected = index; self.history.clear()
        label = next(k.label for k in self.keys if k.sensor == index)
        self.key_title.set(f'{label}  /  sensor {index}')
        if self.snapshot and self.snapshot.count == 61:
            self.press.set(str(self.snapshot.press[index])); self.release.set(str(self.snapshot.release[index]))
            if self.snapshot.version >= 4: self.midi_note.set(note_name(self.snapshot.midi_mapping[index]))
        self.paint()

    def usable(self):
        return bool(not self.demo and self.connection and self.connection.connected and self.snapshot and
                    self.snapshot.profile == 1 and self.snapshot.count == 61 and
                    self.connection.snapshot() and time.monotonic()-self.connection.snapshot()[0] < 1)

    def paint(self,stale=False):
        s = self.snapshot
        for index,(rect,text,velocity) in self.items.items():
            valid = s and s.profile == 1 and s.count == 61 and not stale
            down = valid and s.down[index]
            self.canvas.itemconfigure(rect,fill='#a95420' if down else '#21313e' if valid else '#26303a',
                                      outline='#56d7db' if index == self.selected else '#354958')
            self.canvas.itemconfigure(text,text=str(s.raw[index]) if valid else '—')
            result = 'v —'
            if valid and s.version >= 2 and s.velocity_state[index] & 2:
                result = f'v{s.velocity[index]:.3f}' if s.version >= 3 else f'v{s.velocity[index]:+d}'
            self.canvas.itemconfigure(velocity,text=result)
        for key in self.keys:
            label = key.label
            if s and s.version >= 4 and s.count == 61:
                suffix = {'Fn':'mode','LCt':'−8','LAl':'+8'}.get(label,note_name(s.midi_mapping[key.sensor]))
                label += '/'+suffix
            if key.sensor in self.titles: self.canvas.itemconfigure(self.titles[key.sensor],text=label)
        if s and s.count == 61:
            i = self.selected
            velocity = 'Velocity: requires keyboard-velocity firmware'
            if s.version >= 2:
                state = s.velocity_state[i]
                result = 'no completed fit'
                if state & 2:
                    result = f'{s.velocity[i]:.6f} [0–1]' if s.version >= 3 else f'{s.velocity[i]:+d} counts/s'
                velocity = f'Velocity: {result}  (assumed 8 kHz)\nFits: {s.captures[i]}  |  '
                velocity += ('pending; ' if state & 4 else '') + ('armed' if state & 1 else 'waiting for release')
            self.details.set(f'Raw: {s.raw[i]}   Sensor: {"DOWN" if s.down[i] else "up"}\n'
                             f'On device: press {s.press[i]}, release {s.release[i]}\n'
                             f'Config revision: {s.revision}\n'
                             f'{velocity}\n'
                             f'HID submitted: {s.report.hex()}' +
                             (f'\nMIDI base: {note_name(s.midi_mapping[i])} ({s.midi_mapping[i] if s.midi_mapping[i] != 255 else "unmapped"}); octave {s.octave:+d}' if s.version >= 4 else ''))
        self.graph.delete('all')
        w,h = max(1,self.graph.winfo_width()),max(1,self.graph.winfo_height())
        if s and s.count == 61:
            for value,color,title in ((s.press[self.selected],'#f1a366','press'),(s.release[self.selected],'#56d7db','release')):
                y = h-15-value/4096*(h-30)
                self.graph.create_line(0,y,w,y,fill=color,dash=(4,4))
                self.graph.create_text(6,y+10 if title == 'press' else y-10,anchor='w',text=f'{title} {value}',fill=color)
        if len(self.history)>1:
            points = []
            for i,value in enumerate(self.history): points.extend((i/(len(self.history)-1)*(w-2),h-15-value/4096*(h-30)))
            self.graph.create_line(*points,fill='#e9f0f4',width=2)

    def toggle_connection(self):
        if self.connection and self.connection.is_alive():
            self.connection.stop(); self.message.set('Disconnecting…'); return
        self.connection = Connection(self.device.get().strip())
        self.initial_fields = False; self.snapshot = None; self.last_sequence = None
        self.connection.start(); self.message.set('Connecting; requesting GUI stream and acknowledged readback…')

    def apply(self):
        try:
            pair = validate_pair(int(self.press.get()),int(self.release.get()))
            if not self.usable(): raise ValueError('Connect to a fresh ANSI keyboard snapshot first.')
            self.connection.submit('set',self.selected,*pair)
            self.message.set('Threshold change queued; waiting for device ACK/readback…')
        except (ValueError,queue.Full) as error: messagebox.showerror('Thresholds',str(error))

    def enable(self,enabled):
        if self.usable():
            try: self.connection.submit('enable',int(enabled))
            except queue.Full: messagebox.showerror('Busy','Configuration queue is full.')

    def apply_midi(self):
        try:
            if not self.usable() or self.snapshot.version < 4: raise ValueError('Connect to keyboard-midi firmware first.')
            label = next(k.label for k in self.keys if k.sensor == self.selected)
            if label in ('Fn','LCt','LAl'): raise ValueError('This key is a reserved MIDI mode/octave control.')
            self.connection.submit('midi',self.selected,parse_note(self.midi_note.get()))
            self.message.set('MIDI mapping queued; waiting for device ACK/readback…')
        except (ValueError,queue.Full) as error: messagebox.showerror('MIDI mapping',str(error))

    def apply_all(self):
        try:
            pair = validate_pair(int(self.press.get()),int(self.release.get()))
            if not self.usable() or self.snapshot.version < 2:
                raise ValueError('Apply-all requires connected keyboard-velocity firmware.')
            if not messagebox.askyesno('Apply to all keys',f'Set all 61 keys to press {pair[0]}, release {pair[1]}?\nThis releases held keys and waits for neutral.'):
                return
            self.connection.submit('all',*pair)
            self.message.set('All-key thresholds queued; waiting for ACK and all 61 readbacks…')
        except (ValueError,queue.Full) as error: messagebox.showerror('Thresholds',str(error))

    def save_profile(self):
        try:
            if not self.snapshot: raise ValueError('No device configuration to save.')
            profile = profile_from_snapshot(self.snapshot)
            path = filedialog.asksaveasfilename(defaultextension='.json',filetypes=[('Keyboard profile','*.json')])
            if path:
                Path(path).write_text(json.dumps(profile,indent=2)+'\n')
                self.message.set('Saved device-confirmed thresholds to '+path)
        except (ValueError,OSError) as error: messagebox.showerror('Save profile',str(error))

    def load_profile(self):
        if not self.usable(): return
        path = filedialog.askopenfilename(filetypes=[('Keyboard profile','*.json')])
        if not path: return
        try:
            profile = json.loads(Path(path).read_text())
            values = validate_profile(profile)
            if profile['version'] == 2 and self.snapshot.version < 4: raise ValueError('MIDI profile requires keyboard-midi firmware.')
            if not messagebox.askyesno('Apply profile','Temporarily disable keyboard output and apply all 61 keys? Settings are RAM-only.'):
                return
            enabled = bool(self.snapshot.flags & 1)
            if not self.connection.requests.empty(): raise ValueError('Wait for queued changes to finish first.')
            self.connection.submit('enable',0)
            for index,pair in sorted(values.items()): self.connection.submit('set',index,*pair)
            if profile['version'] == 2:
                for key in profile['keys']:
                    if key['label'] not in ('Fn','LCt','LAl'): self.connection.submit('midi',key['sensor'],key['midi'])
            self.connection.submit('enable',int(enabled))
            self.message.set('Applying profile with per-key readback; a failure cancels remaining changes.')
        except (ValueError,OSError,queue.Full) as error: messagebox.showerror('Load profile',str(error))

    def update(self):
        stale = True
        if self.demo:
            raw = [3900]*61
            raw[32] = int(3900-2600*(.5+.5*math.sin(time.monotonic()*2)))
            self.snapshot = Snapshot(1,61,7,1,int(time.monotonic()*30),0,0,0,0,
                                     tuple(raw),(3600,)*61,(3700,)*61,tuple(v<3600 for v in raw),bytes(16),
                                     velocity=tuple(0.5 if i == 32 else 0.0 for i in range(61)),
                                     captures=tuple(1 if i == 32 else 0 for i in range(61)),
                                     velocity_state=tuple(2 if i == 32 else 1 for i in range(61)),version=3)
            stale = False
        elif self.connection:
            latest = self.connection.snapshot()
            if latest:
                self.snapshot = latest[1]; stale = time.monotonic()-latest[0]>1 or not self.connection.connected
            while True:
                try: self.message.set(self.connection.events.get_nowait())
                except queue.Empty: break
            self.connect_button.configure(text='Disconnect' if self.connection.is_alive() else 'Connect')
        s = self.snapshot
        if s:
            if s.count == 61 and not self.initial_fields:
                self.select(self.selected); self.initial_fields = True
            if s.sequence != self.last_sequence and s.count == 61 and not stale:
                self.history.append(s.raw[self.selected]); self.last_sequence = s.sequence
            state = 'STALE / disconnected' if stale else 'REPORTING' if s.flags & 2 else 'Waiting for all keys released' if s.flags & 1 else 'Keyboard disabled'
            if not stale and s.mode: state = f'Legacy FN editor {s.mode} — Escape to exit; use GUI for raw thresholds'
            if s.version >= 4:
                state += f' | {"MIDI" if s.performance_mode else "KEYBOARD"} | octave {s.octave:+d} | MIDI errors={s.midi_errors}'
                if s.midi_cleanup: state += ' | MIDI note cleanup pending'
            if s.profile not in (0,1): state = 'Unsupported graphical layout (ANSI only)'
            self.status.set(f'{"DEMO • " if self.demo else ""}{state}  |  {s.count} sensors  |  valid={bool(s.flags & 4)}  |  '
                            f'scan errors={s.scan_errors}  LED errors={s.light_errors}  |  FN={bool(s.flags & 32)}  |  config revision={s.revision}')
        for button in (self.enable_button,self.disable_button,self.apply_button,self.load_button):
            button.configure(state='normal' if self.usable() else 'disabled')
        self.apply_all_button.configure(state='normal' if self.usable() and s.version >= 2 else 'disabled')
        midi_usable = self.usable() and s.version >= 4 and next(k.label for k in self.keys if k.sensor == self.selected) not in ('Fn','LCt','LAl')
        self.midi_button.configure(state='normal' if midi_usable else 'disabled')
        self.paint(stale)
        self.after_id = self.root.after(33,self.update)

    def close(self):
        self.root.after_cancel(self.after_id)
        if self.connection: self.connection.stop(); self.connection.join(timeout=.3)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',default='/dev/ttyACM0')
    parser.add_argument('--demo',action='store_true',help='visual demo only; never opens a device')
    args = parser.parse_args()
    root = tk.Tk(); App(root,args.device,args.demo); root.mainloop()


if __name__ == '__main__': main()
