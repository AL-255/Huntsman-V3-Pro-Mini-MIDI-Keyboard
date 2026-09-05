#!/usr/bin/env python3
"""Optional real Tk UI smoke test under a private Xvfb display, no device access."""
import argparse
import os
import pty
import select
import subprocess
import time
import tkinter as tk
from unittest.mock import patch
from keyboard_gui import App
from test_keyboard_gui import Device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--screenshot',help='optional PNG screenshot (requires Pillow)')
    args = parser.parse_args()
    read_fd,write_fd = os.pipe()
    server = subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1280x900x24','-nolisten','tcp'],
                              pass_fds=(write_fd,),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    os.close(write_fd)
    root = None
    try:
        if not select.select([read_fd],[],[],5)[0]: raise RuntimeError('Xvfb did not start')
        display = os.read(read_fd,32).decode().strip()
        if not display.isdecimal(): raise RuntimeError('Xvfb startup failed')
        os.environ['DISPLAY'] = ':'+display
        root = tk.Tk(); app = App(root,demo=True)
        root.update()
        assert len(app.items) == 61 and len(app.canvas.find_all()) == 244
        assert app.usable() is False
        assert str(app.apply_button['state']) == 'disabled'
        assert '0.500000 [0–1]' in app.details.get()
        assert app.canvas.itemcget(app.items[32][2],'text') == 'v0.500'
        assert str(app.connect_button['state']) == 'disabled'
        for key in app.keys:
            rect,_,_ = app.items[key.sensor]
            x1,y1,x2,y2 = app.canvas.coords(rect)
            assert 0 <= x1 < x2 <= app.canvas.winfo_width()
            assert 0 <= y1 < y2 <= app.canvas.winfo_height()
        key = next(k for k in app.keys if k.label == 'Tab')
        rect,_,_ = app.items[key.sensor]
        x1,y1,x2,y2 = app.canvas.coords(rect)
        app.canvas.event_generate('<Motion>',x=int((x1+x2)/2),y=int((y1+y2)/2))
        app.canvas.event_generate('<Button-1>',x=int((x1+x2)/2),y=int((y1+y2)/2))
        root.update()
        assert app.selected == key.sensor and app.press.get() == '3600'
        app.select(32); root.update()
        if args.screenshot:
            from PIL import ImageGrab
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(args.screenshot)
        root.geometry('930x820'); root.update()
        assert len(app.items) == 61
        assert app.apply_button.winfo_rooty()+app.apply_button.winfo_height() < root.winfo_height()
        assert app.footer.winfo_rooty()+app.footer.winfo_height() < root.winfo_height()
        app.close(); root = None
        print('PASS Tk: 61-key physical geometry, click-to-select, threshold fields, disabled demo controls, resize')
        master,slave = pty.openpty()
        device = Device(master); device.start()
        try:
            root = tk.Tk(); app = App(root,device=os.ttyname(slave))
            app.toggle_connection()
            def pump_until(predicate):
                deadline = time.monotonic()+3
                while time.monotonic() < deadline:
                    root.update()
                    if predicate(): return
                    time.sleep(.01)
                raise AssertionError('GUI condition timed out')
            pump_until(app.usable)
            app.select(32); app.press.set('3000'); app.release.set('3250')
            app.apply_button.invoke()
            pump_until(lambda:app.snapshot.press[32] == 3000 and app.snapshot.release[32] == 3250)
            assert 'press 3000, release 3250' in app.details.get()
            with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                app.apply_all_button.invoke()
            pump_until(lambda:app.snapshot.press == (3000,)*61 and app.snapshot.release == (3250,)*61)
            app.midi_note.set('C4'); app.midi_button.invoke()
            pump_until(lambda:app.snapshot.midi_mapping[32] == 60)
            assert 'C4 (60)' in app.details.get()
            app.select(next(k.sensor for k in app.keys if k.label == 'Fn'))
            root.update(); pump_until(lambda:str(app.midi_button['state']) == 'disabled')
            assert str(app.midi_button['state']) == 'disabled'
            app.disable_button.invoke()
            pump_until(lambda:not app.snapshot.flags & 1)
            app.toggle_connection()
            pump_until(lambda:not app.connection.is_alive())
            assert not app.usable()
            app.close(); root = None
            print('PASS Tk+PTY: connect, select A, apply pair/all, device-confirmed panel, disable, disconnect')
        finally:
            device.stop_event.set(); device.join(1)
            os.close(master); os.close(slave)
    finally:
        if root: root.destroy()
        os.close(read_fd)
        server.terminate(); server.wait(timeout=3)


if __name__ == '__main__': main()
