#!/usr/bin/env python3
"""Click the original General settings checkbox in a real AppImage on Xvfb."""
import ctypes
import ctypes.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

image_source=Path(sys.argv[1]).resolve()
probe=Path(sys.argv[2]).resolve()
assert 'fxsound-isolated.' in os.environ.get('PIPEWIRE_RUNTIME_DIR','')
work=Path(tempfile.mkdtemp(prefix='login-gui-',dir=Path(os.environ['PIPEWIRE_RUNTIME_DIR']).parent))
image=work/'Fx Sound=100% "音響".AppImage'
shutil.copy2(image_source,image)
config=work/'config';entry=config/'autostart/fxsound.desktop'
environment=dict(os.environ,XDG_CONFIG_HOME=str(config),APPIMAGE_EXTRACT_AND_RUN='1')
if not ctypes.util.find_library('X11') or not ctypes.util.find_library('Xtst'):sys.exit(77)
x11=ctypes.CDLL(ctypes.util.find_library('X11'));xtest=ctypes.CDLL(ctypes.util.find_library('Xtst'))
x11.XOpenDisplay.restype=ctypes.c_void_p
x11.XSync.argtypes=[ctypes.c_void_p,ctypes.c_int]
x11.XKeysymToKeycode.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
xtest.XTestFakeMotionEvent.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_ulong]
xtest.XTestFakeButtonEvent.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_int,ctypes.c_ulong]
xtest.XTestFakeKeyEvent.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_int,ctypes.c_ulong]
display=x11.XOpenDisplay(None);assert display

def run(*args):return subprocess.run(args,capture_output=True,text=True,timeout=8)
def wait(test,message):
    until=time.monotonic()+15
    while time.monotonic()<until:
        found=test()
        if found:return found
        time.sleep(.1)
    raise AssertionError(message)
def bus():
    names=run('gdbus','call','--session','-d','org.freedesktop.DBus','-o','/org/freedesktop/DBus','-m','org.freedesktop.DBus.ListNames').stdout
    found=re.search(r'org.kde.StatusNotifierItem[-:]\d+[-:]\d+',names)
    return found.group() if found else ''
def menu():return run('gdbus','call','--session','-d',bus(),'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.GetLayout','--','0','-1','[]').stdout
def event(item):
    result=run('gdbus','call','--session','-d',bus(),'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.Event','--',str(item),'clicked','<0>','0')
    assert result.returncode==0,result.stderr
def dialog():
    tree=run('xwininfo','-root','-tree').stdout
    for match in re.finditer(r'^\s*(0x[\da-f]+).*?\s(\d+)x(\d+)([+-]\d+)([+-]\d+)',tree,re.M):
        if 600<=int(match[2])<=650 and 550<=int(match[3])<=650:
            # JUCE creates the native window before entering its modal loop.
            # Click only after mapping, so input cannot go to the root window.
            if 'Map State: IsViewable' in run('xwininfo','-id',match[1]).stdout:
                return int(match[4]),int(match[5])
def click(x,y):
    xtest.XTestFakeMotionEvent(display,-1,x,y,0)
    x11.XSync(display,0);time.sleep(.03)
    xtest.XTestFakeButtonEvent(display,1,1,0)
    x11.XSync(display,0);time.sleep(.03)
    xtest.XTestFakeButtonEvent(display,1,0,0)
    x11.XSync(display,0);time.sleep(.2)
def escape():
    key=x11.XKeysymToKeycode(display,0xff1b)
    xtest.XTestFakeKeyEvent(display,key,1,0);xtest.XTestFakeKeyEvent(display,key,0,0);x11.XSync(display,0)
def settings():
    event(7);x,y=wait(dialog,'settings dialog')
    # Constants from the original SettingsComponent/FxWindow layout at 96 DPI:
    # shadow=5, title=56, General button y=110, pane x=153, launch row y=100.
    click(x+50,y+192)
    return x,y

def quit_app():
    layout=menu()
    match=re.search(r"\((\d+), \{[^}]*'label': <'Exit'>",layout)
    assert match,layout
    event(int(match[1]));wait(lambda:not bus(),'app did not exit')

with (work/'app.log').open('w') as log:
    process=subprocess.Popen([str(image),'--autostart'],env=environment,stdout=log,stderr=log,start_new_session=True)
try:
    wait(bus,'AppImage tray registration')
    x,y=settings();click(x+193,y+178)
    wait(entry.exists,'Linux checkbox did not create autostart entry')
    contents=entry.read_text()
    assert 'X-FxSound-LaunchKind=appimage' in contents,contents
    assert str(image) in contents,contents
    assert '/tmp/.mount' not in contents and 'appimage_extracted' not in contents,contents
    print('PASS: original Linux settings checkbox saves persistent AppImage path',flush=True)
    if shutil.which('ffmpeg'):
        run('ffmpeg','-v','error','-f','x11grab','-video_size','1280x800','-i',os.environ['DISPLAY'],'-frames:v','1','-y',str(work/'general-settings.png'))
    click(x+193,y+178);wait(lambda:not entry.exists(),'checkbox did not remove entry')
    click(x+193,y+178);wait(entry.exists,'checkbox did not reenable entry')
    escape();wait(lambda:not dialog(),'settings dialog close')
    quit_app();process.wait(timeout=10)
    subprocess.run([str(probe),'launch',str(entry)],env=environment,check=True)
    wait(bus,'saved Desktop Entry failed to relaunch AppImage')
    time.sleep(.3)
    # Only the hidden main window exists; a settings-sized window must not.
    assert not dialog()
    tree=run('xwininfo','-root','-tree').stdout
    for window in re.findall(r'^\s*(0x[\da-f]+)\s+"FxSound"',tree,re.M):
        assert 'Map State: IsViewable' not in run('xwininfo','-id',window).stdout, 'autostart reopened the main window'
    quit_app()
    print('PASS: saved AppImage Desktop Entry relaunches, remains minimized and exits cleanly',flush=True)
finally:
    if shutil.which('ffmpeg'):
        run('ffmpeg','-v','error','-f','x11grab','-video_size','1280x800','-i',os.environ['DISPLAY'],'-frames:v','1','-y',str(work/'final-state.png'))
    if bus():
        try:escape();quit_app()
        except Exception:
            match=re.search(r'Item[-:](\d+)[-:]',bus())
            if match:
                pid=int(match[1])
                environ=Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
                if os.fsencode('XDG_CONFIG_HOME='+str(config)) in environ:os.kill(pid,15)
    if process.poll() is None:
        os.killpg(process.pid,15);process.wait(timeout=8)
    print('Login GUI artifacts:',work,flush=True)
