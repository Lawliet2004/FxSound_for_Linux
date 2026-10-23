#!/usr/bin/env python3
"""Restart only the servers owned by run-isolated.sh; verify the actual GUI recovers."""
import json
import math
import struct
import wave
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

runtime = Path(os.environ.get('PIPEWIRE_RUNTIME_DIR', ''))
if 'fxsound-isolated.' not in str(runtime):
    sys.exit(77)
app_path = str(Path(sys.argv[1]).resolve())
delayed_start = '--late-start' in sys.argv[2:]
initial_bypass = '--bypass' in sys.argv[2:]
parent = os.getppid()
for _ in range(6):
    children = Path(f'/proc/{parent}/task/{parent}/children').read_text().split()
    original_servers = {Path(f'/proc/{pid}/comm').read_text().strip(): int(pid) for pid in children if Path(f'/proc/{pid}/comm').exists()}
    if all(name in original_servers for name in ['pipewire', 'pipewire-pulse', 'wireplumber']): break
    parent = int(re.search(r'^PPid:\s+(\d+)', Path(f'/proc/{parent}/status').read_text(), re.M).group(1))
assert all(name in original_servers for name in ['pipewire', 'pipewire-pulse', 'wireplumber']), 'Runner-owned server PIDs unavailable'
original_servers = {name: original_servers[name] for name in ['pipewire', 'pipewire-pulse', 'wireplumber']}
for name in ['pipewire', 'pipewire-pulse', 'wireplumber']:
    env = Path(f'/proc/{original_servers[name]}/environ').read_bytes().split(b'\0')
    assert os.fsencode(f'PIPEWIRE_RUNTIME_DIR={runtime}') in env, 'Server is outside the private session'
servers = []
app = None
work = Path(tempfile.mkdtemp(prefix='restart-', dir=runtime.parent))

def run(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=5)
    except subprocess.TimeoutExpired:
        # A socket can exist before the replacement server services clients.
        # Let the enclosing readiness deadline retry rather than aborting early.
        return subprocess.CompletedProcess(args, 124, '', 'command timed out')

def wait(test, message, seconds=15):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if test():
            print('PASS:', message, flush=True)
            return
        time.sleep(.1)
    raise AssertionError(message)

def graph():
    result = run('pw-dump')
    return json.loads(result.stdout) if result.returncode == 0 else []

def routed():
    objs = graph()
    names = {o['id']:o.get('info', {}).get('props', {}).get('node.name') for o in objs if o['type'] == 'PipeWire:Interface:Node'}
    fx = [key for key,value in names.items() if value in ('FxSound', 'FxSound Processor')]
    links = [o['info'] for o in objs if o['type'] == 'PipeWire:Interface:Link' and (o['info']['input-node-id'] in fx or o['info']['output-node-id'] in fx)]
    output = {names.get(link['input-node-id']) for link in links if names.get(link['output-node-id']) == 'FxSound Processor'}
    return len(fx) == 2 and len(links) == 4 and output == {'fxsound_test_output'}

def virtual_identity():
    for obj in graph():
        props = obj.get('info', {}).get('props', {})
        if obj['type'] == 'PipeWire:Interface:Node' and props.get('node.name') == 'FxSound':
            return obj['id'], props.get('object.serial')
    return None

def bus_name():
    names = run('gdbus','call','--session','-d','org.freedesktop.DBus','-o','/org/freedesktop/DBus','-m','org.freedesktop.DBus.ListNames').stdout
    found = re.search(r'org.kde.StatusNotifierItem[-:]' + str(app.pid) + r'[-:]\d+', names)
    return found.group() if found else ''

def menu():
    bus = bus_name()
    return run('gdbus','call','--session','-d',bus,'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.GetLayout','--','0','-1','[]').stdout if bus else ''

def toggle():
    result=run('gdbus','call','--session','-d',bus_name(),'-o','/com/fxsound/MenuBar','-m','com.canonical.dbusmenu.Event','--','2','clicked','<0>','0')
    assert result.returncode == 0, result.stderr

def check_audio(cycle):
    # Capture the actual output sink monitor, not just the DSP node. This
    # verifies that recovered links carry samples through the complete path.
    tone = work/'tone.wav'
    with wave.open(str(tone), 'wb') as wav:
        wav.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        wav.writeframes(b''.join(struct.pack('<hh', *(2*[int(8000*math.sin(2*math.pi*1000*i/48000))])) for i in range(48000)))
    capture = work/f'capture-{cycle}.wav'
    rec = subprocess.Popen(['pw-record','-P','{ node.name=restart-capture node.autoconnect=false }','--rate','48000','--channels','2','--format','s16',str(capture)], stdout=subprocess.DEVNULL, stderr=(work/f'recorder-{cycle}.log').open('w'))
    try:
        wait(lambda: 'restart-capture:input_FL' in run('pw-link','-i').stdout, 'capture ports available')
        for channel in ['FL','FR']:
            result = run('pw-link',f'fxsound_test_output:monitor_{channel}',f'restart-capture:input_{channel}')
            assert result.returncode == 0, result.stderr
        result = run('pw-play','--target','FxSound',str(tone))
        assert result.returncode == 0, result.stderr
        time.sleep(.2)
        assert rec.poll() is None, 'capture exited before the requested stop'
        rec.send_signal(signal.SIGINT)
        code = rec.wait(timeout=5)
        # pw-cat 1.6.2 returns 1 after SIGINT unless playback drained; capture
        # never drains. Verify the recorder log and actual samples separately.
        # https://github.com/PipeWire/pipewire/blob/1.6.2/src/tools/pw-cat.c
        assert code in (0, 1, -signal.SIGINT, 128 + signal.SIGINT), f'capture failed with exit {code}'
        assert 'error:' not in (work/f'recorder-{cycle}.log').read_text().lower()
        with wave.open(str(capture), 'rb') as wav:
            samples = struct.unpack('<' + 'h'*(wav.getnframes()*2), wav.readframes(wav.getnframes()))
        active = [sample for sample in samples if abs(sample) > 50]
        assert len(active) > 48000, 'no sustained audio at recovered physical output'
        if cycle:
            peak = max(map(abs, samples))
            assert 7800 <= peak <= 8200, f'recovered bypass changed gain: peak {peak}'
        print('PASS: audio reaches output monitor after restart' + (' with transparent bypass gain' if cycle else ''), flush=True)
    finally:
        if rec.poll() is None: rec.kill(); rec.wait()

try:
    if delayed_start:
        for pid in original_servers.values():
            os.kill(pid, signal.SIGTERM)
        wait(lambda: not (runtime/'pipewire-0').exists(), 'private core stopped before login launch')
    if initial_bypass:
        settings = work/'config/FxSound/FxSound.settings'
        settings.parent.mkdir(parents=True)
        settings.write_text('<PROPERTIES><VALUE name="power" val="0"/></PROPERTIES>')
    app_env = dict(os.environ, XDG_CONFIG_HOME=str(work/'config'))
    with (work/'app.log').open('w') as log:
        app = subprocess.Popen([app_path] + (['--autostart'] if delayed_start else []), env=app_env, stdout=log, stderr=subprocess.STDOUT)
    if delayed_start:
        time.sleep(3)  # Exercise multiple retries with no PipeWire socket.
        assert app.poll() is None, 'login launch exited before audio service startup'
        with (work/'core-delayed.log').open('w') as log:
            servers.append(subprocess.Popen(['pipewire'], stdout=log, stderr=subprocess.STDOUT))
        wait(lambda: (runtime/'pipewire-0').exists(), 'private core starts without session manager')
        time.sleep(3)  # A core alone has no WirePlumber default metadata.
        assert app.poll() is None, 'login launch exited before metadata was available'
        for name in ['pipewire-pulse', 'wireplumber']:
            with (work/f'{name}-delayed.log').open('w') as log:
                servers.append(subprocess.Popen([name], stdout=log, stderr=subprocess.STDOUT))
        wait(lambda: run('pactl','info').returncode == 0, 'private Pulse server becomes ready')
        assert run('pactl','load-module','module-null-sink','sink_name=fxsound_test_output').returncode == 0
        wait(lambda: run('pactl','set-default-sink','fxsound_test_output').returncode == 0, 'delayed output becomes configurable')
    wait(routed, 'application routes before restart')
    wait(lambda: ('Turn On' if initial_bypass else 'Turn Off') in menu(), 'application preserves saved power state')
    if delayed_start:
        tree = run('xwininfo','-root','-tree').stdout
        windows = re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+"FxSound"', tree, re.M)
        assert not any('Map State: IsViewable' in run('xwininfo','-id',window).stdout for window in windows), 'delayed login unexpectedly opened a window'
        check_audio(1 if initial_bypass else 0)
        print('PASS: login before audio services recovers original DSP without a visible error dialog', flush=True)
    if initial_bypass:
        toggle()
        wait(lambda: 'Turn Off' in menu(), 'enable DSP after delayed bypass startup')
    for cycle in range(2):
        if cycle:
            toggle()
            wait(lambda: 'Turn On' in menu(), 'bypass enabled before second restart')
        pids = [server.pid for server in servers] if servers else list(original_servers.values())
        # Only runner-owned siblings in the first cycle and our own children in
        # the second cycle are ever signalled. Never discover servers by name.
        for pid in pids:
            if pid in [original_servers.get(name) for name in ['pipewire','pipewire-pulse','wireplumber']] or any(server.pid == pid for server in servers):
                try: os.kill(pid, signal.SIGTERM)
                except ProcessLookupError: pass
        def output_missing_from_tray():
            layout = menu()
            return 'Playback Device Select' in layout and 'fxsound_test_output' not in layout
        wait(output_missing_from_tray, 'UI observes output unavailability before recovery')
        assert app.poll() is None, 'application exited when private servers stopped'
        print('PASS: application survives audio server outage', flush=True)
        for server in servers:
            server.wait(timeout=5)
        servers.clear()
        for name in ['pipewire','pipewire-pulse','wireplumber']:
            with (work/f'{name}-{cycle}.log').open('w') as log:
                servers.append(subprocess.Popen([name], stdout=log, stderr=subprocess.STDOUT))
            if name == 'pipewire':
                wait(lambda: run('pw-dump').returncode == 0, 'private PipeWire core returns')
            elif name == 'pipewire-pulse':
                wait(lambda: run('pactl','info').returncode == 0, 'private Pulse server returns')
        assert run('pactl','load-module','module-null-sink','sink_name=fxsound_test_output').returncode == 0
        wait(lambda: run('pactl','set-default-sink','fxsound_test_output').returncode == 0, 'physical output becomes configurable')
        wait(routed, 'same application restores all four audio links', seconds=25)
        assert app.poll() is None
        check_audio(cycle)
        if cycle:
            assert 'Turn On' in menu(), 'recovery changed bypass state'
            print('PASS: bypass state preserved through restart', flush=True)
        else:
            assert 'Turn Off' in menu(), 'recovery changed enabled state'
    # A session-manager restart removes its metadata while PipeWire survives.
    old_identity = virtual_identity()
    servers[2].terminate(); servers[2].wait(timeout=5)
    time.sleep(.2)
    assert app.poll() is None, 'application exited with WirePlumber'
    with (work/'wireplumber-only.log').open('w') as log:
        servers[2] = subprocess.Popen(['wireplumber'], stdout=log, stderr=subprocess.STDOUT)
    wait(lambda: virtual_identity() is not None and virtual_identity() != old_identity and routed(), 'WirePlumber-only restart restores transport', seconds=25)
    check_audio(1)

    # Recreate an externally deleted front-end, keeping the same GUI/DSP alive.
    old_identity = virtual_identity()
    assert run('pw-cli', 'destroy', str(old_identity[0])).returncode == 0
    wait(lambda: virtual_identity() is not None and virtual_identity() != old_identity and routed(), 'externally deleted FxSound sink is recreated', seconds=25)
    check_audio(1)
    app.send_signal(signal.SIGTERM)
    assert app.wait(timeout=8) == 0
    wait(lambda: run('pactl','get-default-sink').stdout.strip() == 'fxsound_test_output', 'physical default usable after recovery and shutdown')
    print('SERVER RESTART: PASS', flush=True)
finally:
    if app and app.poll() is None: app.kill(); app.wait()
    for server in servers:
        if server.poll() is None: server.terminate()
    for server in servers:
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)
    print('Restart test logs:', work, flush=True)
