#!/usr/bin/env python3
"""Measure Firefox/Chromium/Chrome/ffplay through FxSound in a private audio session."""
import functools
import glob
import http.server
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave

if 'fxsound-isolated.' not in os.environ.get('PIPEWIRE_RUNTIME_DIR',''):
    sys.exit(77)
app_path=str(Path(sys.argv[1]).resolve())
chromium=os.environ.get('FXSOUND_CHROMIUM') or shutil.which('chromium') or shutil.which('chromium-browser')
if not chromium:
    candidates=glob.glob(str(Path.home()/'.cache/ms-playwright/chromium-*/chrome-linux64/chrome'))
    chromium=sorted(candidates)[-1] if candidates else None
clients=[('Firefox',os.environ.get('FXSOUND_FIREFOX') or shutil.which('firefox')),('Chromium',chromium),('Chrome',shutil.which('google-chrome')),('ffplay',shutil.which('ffplay'))]
requested=os.environ.get('FXSOUND_CLIENTS')
if requested:
    selected=requested.split(',')
    assert set(selected)<={name for name,_ in clients}, 'unknown client selection'
    clients=[client for client in clients if client[0] in selected]
if any(not path for _,path in clients):
    print('Missing clients:',[name for name,path in clients if not path]);sys.exit(77)
versions={}
for name,executable in clients:
    version=subprocess.run([executable,'-version' if name=='ffplay' else '--version'],capture_output=True,text=True,timeout=20)
    versions[name]=version.stdout.strip().splitlines()[0] if version.stdout.strip() else version.stderr.strip().splitlines()[0]
    if name=='Chromium' and not versions[name].startswith('Chromium '):
        print('An unbranded Chromium binary is required; set FXSOUND_CHROMIUM. Found:',versions[name])
        sys.exit(77)
work=Path(tempfile.mkdtemp(prefix='desktop-clients-',dir=Path(os.environ['PIPEWIRE_RUNTIME_DIR']).parent))
processes=[]

def run(*args):
    return subprocess.run(args,capture_output=True,text=True,timeout=8)

def wait(test,description,seconds=20):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        value=test()
        if value:return value
        time.sleep(.1)
    raise AssertionError(description)

def graph():return json.loads(run('pw-dump').stdout)

def streams_to(target):
    data=graph()
    nodes={o['id']:o['info']['props'] for o in data if o['type']=='PipeWire:Interface:Node'}
    sink=next((key for key,props in nodes.items() if props.get('node.name')==target),None)
    sources={o['info']['output-node-id'] for o in data if o['type']=='PipeWire:Interface:Link' and o['info']['input-node-id']==sink}
    return [nodes[key] for key in sources if nodes[key].get('media.class')=='Stream/Output/Audio']

def routed_streams():return streams_to('FxSound')

def bus():
    names=run('gdbus','call','--session','-d','org.freedesktop.DBus','-o','/org/freedesktop/DBus','-m','org.freedesktop.DBus.ListNames').stdout
    found=re.search(r'org.kde.StatusNotifierItem[-:]'+str(app.pid)+r'[-:]\d+',names)
    return found.group() if found else ''

def menu():return run('gdbus','call','--session','-d',bus(),'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.GetLayout','--','0','-1','[]').stdout

def toggle():
    result=run('gdbus','call','--session','-d',bus(),'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.Event','--','2','clicked','<0>','0')
    assert result.returncode==0,result.stderr

def capture(label):
    path=work/(label+'.wav')
    with (work/(label+'.log')).open('w') as log:
        rec=subprocess.Popen(['pw-record','-P','{ node.name=client-capture node.autoconnect=false }','--rate','48000','--channels','2','--format','s16',str(path)],stdout=log,stderr=log)
    processes.append(rec)
    wait(lambda:'client-capture:input_FL' in run('pw-link','-i').stdout,'recorder ports')
    for channel in ['FL','FR']:
        link=run('pw-link',f'fxsound_test_output:monitor_{channel}',f'client-capture:input_{channel}')
        assert link.returncode==0,link.stderr
    time.sleep(2)
    assert rec.poll() is None,'recorder exited early'
    rec.send_signal(signal.SIGINT);rec.wait(timeout=5)
    with wave.open(str(path)) as wav:
        assert wav.getnchannels()==2 and wav.getframerate()==48000
        count=wav.getnframes();pcm=struct.unpack('<'+'h'*(2*count),wav.readframes(count))
    left,right=pcm[0::2],pcm[1::2]
    active=[i for i,value in enumerate(left) if abs(value)>40]
    assert len(active)>48000,'no sustained samples from client at physical output monitor'
    a,b=active[0],active[-1];segment=range(a+(b-a)//4,b-(b-a)//4)
    rms=lambda channel:math.sqrt(sum(channel[i]**2 for i in segment)/len(segment))
    return rms(left),rms(right),max(map(abs,pcm))

def terminate(process):
    if process.poll() is None:
        os.killpg(process.pid,signal.SIGTERM)
        try:process.wait(timeout=8)
        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()

class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(work)))
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    with wave.open(str(work/'tone.wav'),'wb') as wav:
        wav.setparams((2,2,48000,0,'NONE','not compressed'))
        wav.writeframes(b''.join(struct.pack('<hh',int(6000*math.sin(2*math.pi*500*i/48000)),int(3000*math.sin(2*math.pi*1000*i/48000))) for i in range(48000*4)))
    (work/'index.html').write_text('<!doctype html><title>FxSound audio fixture</title><audio src="tone.wav" loop autoplay controls></audio><script>document.querySelector("audio").play()</script>')
    url=f'http://127.0.0.1:{server.server_port}/index.html'
    def launch_app():
        with (work/'app.log').open('a') as log:
            launched=subprocess.Popen([app_path,'--autostart','--preset=Streaming Video'],env=dict(os.environ,XDG_CONFIG_HOME=str(work/'app-config')),stdout=log,stderr=log,start_new_session=True)
        processes.append(launched)
        return launched

    # Default-policy migration must also work for a client that was already
    # playing before FxSound started, and return it after shutdown.
    with (work/'existing-player.log').open('w') as log:
        existing=subprocess.Popen([shutil.which('ffplay'),'-nodisp','-loglevel','error','-loop','0',str(work/'tone.wav')],stdout=log,stderr=log,start_new_session=True)
    processes.append(existing)
    wait(lambda:streams_to('fxsound_test_output'),'existing player at original output')
    baseline=capture('before-start')
    app=launch_app()
    wait(lambda:bus() and 'Turn Off' in menu(),'app initialized')
    wait(routed_streams,'existing player did not move to FxSound')
    processed=capture('existing-enhanced')
    assert abs(20*math.log10(processed[0]/baseline[0]))>1,'existing playback did not reach DSP'
    terminate(app)
    wait(lambda:streams_to('fxsound_test_output'),'existing player did not return to original output')
    restored=capture('after-exit')
    assert abs(20*math.log10(restored[0]/baseline[0]))<.2,'shutdown did not restore original playback gain'
    terminate(existing)
    print('PASS: existing playback migrates into FxSound and returns after shutdown with original gain',flush=True)
    app=launch_app()
    wait(lambda:bus() and 'Turn Off' in menu(),'app restarted')
    results=[]
    for name,executable in clients:
        profile=work/name;profile.mkdir()
        if name=='Firefox':
            (profile/'user.js').write_text('user_pref("media.autoplay.default",0);\nuser_pref("media.autoplay.block-webaudio",false);\nuser_pref("media.cubeb.backend","pulse");\nuser_pref("browser.shell.checkDefaultBrowser",false);\n')
            command=[executable,'--headless','--no-remote','--profile',str(profile),url]
        elif name=='ffplay':command=[executable,'-nodisp','-loglevel','error','-loop','0',str(work/'tone.wav')]
        else:command=[executable,'--headless=new','--no-sandbox','--disable-gpu','--no-first-run','--disable-background-networking','--disable-sync','--password-store=basic','--autoplay-policy=no-user-gesture-required','--user-data-dir='+str(profile),url]
        with (work/(name+'.log')).open('w') as log:
            client=subprocess.Popen(command,stdout=log,stderr=log,start_new_session=True)
        processes.append(client)
        def live_streams():
            assert client.poll() is None, name+' exited; see '+str(work/(name+'.log'))
            return routed_streams()
        streams=wait(live_streams,name+' did not route to default FxSound sink')
        assert client.poll() is None,name+' exited'
        on=capture(name+'-enabled')
        toggle();wait(lambda:'Turn On' in menu(),'bypass state')
        off=capture(name+'-bypass')
        gain=20*math.log10(on[0]/off[0])
        assert abs(gain)>1,name+' DSP did not measurably change output'
        assert abs(20*math.log10(off[0]/(6000/math.sqrt(2))))<.2,(name,'bypass left gain',off)
        assert abs(20*math.log10(off[1]/(3000/math.sqrt(2))))<.2,(name,'bypass right gain',off)
        print(f'PASS: {name}: stream -> default FxSound -> DSP -> output monitor; enabled delta {gain:+.2f} dB, transparent stereo bypass',flush=True)
        results.append(dict(client=name,version=versions[name],streams=streams,enabled=on,bypass=off,gain_db=gain,command=command))
        toggle();wait(lambda:'Turn Off' in menu(),'processing reenabled')
        terminate(client)
        wait(lambda:not routed_streams(),'client stream cleanup')
    (work/'results.json').write_text(json.dumps(results,indent=2))
    print('DESKTOP CLIENTS: PASS',flush=True)
finally:
    for process in reversed(processes):
        if process.poll() is None:
            # Recorders are ordinary children; browsers/app own their groups.
            if os.getpgid(process.pid)==process.pid:terminate(process)
            else:process.terminate();process.wait(timeout=5)
    server.shutdown()
    print('Desktop-client test artifacts:',work,flush=True)
