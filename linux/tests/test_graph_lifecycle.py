#!/usr/bin/env python3
"""Real graph regressions. Refuse the host session; use run-isolated.sh."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time

if 'fxsound-isolated.' not in os.environ.get('PIPEWIRE_RUNTIME_DIR', ''):
    print('Private PipeWire session required')
    sys.exit(77)
probe_path = str(Path(sys.argv[1]).resolve())
modules = []
processes = []

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def wait(test, message, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = test()
        if value:
            print('PASS:', message, flush=True)
            return value
        time.sleep(.08)
    raise AssertionError(message)

def dump():
    return json.loads(run('pw-dump'))

def fx_links():
    graph = dump()
    nodes = {o['id'] for o in graph if o.get('info', {}).get('props', {}).get('node.name') in ('FxSound', 'FxSound Processor')}
    return {o['id']: o['info'] for o in graph if o['type'] == 'PipeWire:Interface:Link' and
            (o['info']['output-node-id'] in nodes or o['info']['input-node-id'] in nodes)}

def target():
    graph = dump()
    names = {o['id']: o.get('info', {}).get('props', {}).get('node.name') for o in graph if o['type'] == 'PipeWire:Interface:Node'}
    return {names.get(o['info']['input-node-id']) for o in graph if o['type'] == 'PipeWire:Interface:Link' and names.get(o['info']['output-node-id']) == 'FxSound Processor'}

def configured():
    for obj in dump():
        if obj['type'] == 'PipeWire:Interface:Metadata' and obj.get('props', {}).get('metadata.name') == 'default':
            for item in obj.get('metadata', []):
                if item.get('subject') == 0 and item.get('key') == 'default.configured.audio.sink':
                    value = item.get('value')
                    return value if isinstance(value, dict) else json.loads(value)
    return None

def sink(name, description=None):
    args = ['pactl', 'load-module', 'module-null-sink', f'sink_name={name}']
    if description:
        args.append(f'sink_properties=\'device.description="{description}"\'')
    module = run(*args)
    modules.append(module)
    return module

def read_line(proc):
    data = bytearray()
    while True:
        assert select.select([proc.stdout], [], [], 10)[0], 'probe did not reply'
        char = os.read(proc.stdout.fileno(), 1)
        assert char, 'probe exited'
        if char == b'\n': return data.decode()
        data.extend(char)

def start():
    proc = subprocess.Popen([probe_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None, bufsize=0)
    processes.append(proc)
    assert read_line(proc) == 'READY', 'backend init failed'
    wait(lambda: len(fx_links()) == 4, 'exactly four audio links created')
    return proc

def command(proc, value):
    proc.stdin.write((value + '\n').encode())
    proc.stdin.flush()
    lines = []
    while True:
        line = read_line(proc)
        lines.append(line)
        if line.startswith('AVAILABLE '):
            return lines

def stop(proc):
    proc.stdin.write(b'quit\n')
    proc.stdin.flush()
    assert proc.wait(timeout=5) == 0, 'backend shutdown failed'
    wait(lambda: len(fx_links()) == 0, 'audio links removed on shutdown')

try:
    original = configured()
    assert original and original.get('name') == 'fxsound_test_output', original
    proc = start()
    baseline = set(fx_links())
    identities = {key: value['props']['object.serial'] for key,value in fx_links().items()}
    unrelated = sink('fx_unrelated')
    time.sleep(.2)
    assert {key: value['props']['object.serial'] for key,value in fx_links().items()} == identities, 'unrelated sink addition rebuilt audio links'
    run('pactl', 'unload-module', unrelated); modules.remove(unrelated)
    time.sleep(.2)
    assert {key: value['props']['object.serial'] for key,value in fx_links().items()} == identities, 'unrelated sink removal rebuilt audio links'
    print('PASS: unrelated graph changes preserve all four link IDs and serials', flush=True)

    # Independently deleting a link must repair just that link.
    victim_id, victim = next(iter(fx_links().items()))
    run('pw-link', '-d', str(victim_id))
    wait(lambda: len(fx_links()) == 4 and fx_links().get(victim_id, {}).get('props', {}).get('object.serial') != victim.get('props', {}).get('object.serial'), 'externally removed link repaired')
    command(proc, 'tick')
    assert baseline - {victim_id} <= set(fx_links()), 'repair rebuilt healthy links'

    unicode_module = sink('fx_音響_🎧', '音響 🎧')
    time.sleep(.2)
    unicode_reply = command(proc, 'unicode')
    assert 'UNICODE 1' in unicode_reply, f'UTF-8 ID/description did not decode correctly: {unicode_reply}'
    wait(lambda: target() == {'fx_音響_🎧'}, 'Unicode device selection reaches exact UTF-8 node')
    run('pactl', 'unload-module', unicode_module); modules.remove(unicode_module)
    wait(lambda: target() == {'fxsound_test_output'}, 'selected device removal falls back')
    unicode_module = sink('fx_音響_🎧', '音響 🎧')
    wait(lambda: (command(proc, 'tick'), target() == {'fx_音響_🎧'})[1], 'reconnected device is selected again')
    command(proc, 'default')
    wait(lambda: target() == {'fxsound_test_output'}, 'reset selection follows remembered physical default')
    stop(proc)
    assert configured() == original, 'configured default not restored exactly'
    print('PASS: configured default restored after acknowledged shutdown', flush=True)

    # The name contains FxSound, but is not our virtual sink.
    named = sink('myFxSoundSpeakers')
    run('pactl', 'set-default-sink', 'myFxSoundSpeakers')
    wait(lambda: configured() == {'name': 'myFxSoundSpeakers'}, 'substring-named default selected')
    proc = start(); stop(proc)
    assert configured() == {'name': 'myFxSoundSpeakers'}, 'substring name prevented restoration'
    print('PASS: sink names containing FxSound restore correctly', flush=True)

    # User choice made while running wins over the startup snapshot.
    proc = start()
    run('pactl', 'set-default-sink', 'fxsound_test_output')
    wait(lambda: configured() == original, 'later user default choice observed')
    stop(proc)
    assert configured() == original, 'shutdown overwrote later user default choice'

    # A live clear-all is an explicit user change, not continued ownership.
    proc = start()
    run('pw-metadata', '-d', '0')
    wait(lambda: configured() is None, 'all subject-zero metadata cleared while running')
    command(proc, 'tick')
    stop(proc)
    assert configured() is None, 'shutdown resurrected default after metadata clear-all'
    print('PASS: live metadata clear-all respected at shutdown', flush=True)

    # Clearing an originally absent configured default must remain absent.
    run('pw-metadata', '-d', '0', 'default.configured.audio.sink')
    wait(lambda: configured() is None, 'configured default removed before launch')
    proc = start(); stop(proc)
    assert configured() is None, 'unset configured default was not restored'
    print('PASS: originally unset configured default restored as unset', flush=True)
    print('GRAPH LIFECYCLE: PASS', flush=True)
finally:
    for proc in processes:
        if proc.poll() is None:
            proc.kill(); proc.wait()
    run('pactl', 'set-default-sink', 'fxsound_test_output')
    for module in modules:
        subprocess.run(['pactl', 'unload-module', module], capture_output=True)
