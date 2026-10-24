#!/usr/bin/env python3
"""Crash recovery preserves exact configured defaults and later user choices."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

if 'fxsound-isolated.' not in os.environ.get('PIPEWIRE_RUNTIME_DIR', ''):
    sys.exit(77)
probe = sys.argv[1]
children = []

def configured():
    for obj in json.loads(subprocess.check_output(['pw-dump'], text=True)):
        if obj['type'] == 'PipeWire:Interface:Metadata' and obj.get('props', {}).get('metadata.name') == 'default':
            for item in obj.get('metadata', []):
                if item.get('subject') == 0 and item.get('key') == 'default.configured.audio.sink':
                    value = item.get('value')
                    return value if isinstance(value, dict) else json.loads(value)
    return None

def wait(expected):
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        if configured() == expected: return
        time.sleep(.03)
    raise AssertionError(f'configured default {configured()!r}, expected {expected!r}')

def set_default(value):
    if value is None:
        args = ['pw-metadata', '-n', 'default', '-d', '0', 'default.configured.audio.sink']
    else:
        args = ['pw-metadata', '-n', 'default', '0', 'default.configured.audio.sink', json.dumps(value), 'Spa:String:JSON']
    subprocess.run(args, stdout=subprocess.DEVNULL, check=True)
    wait(value)

def start():
    child = subprocess.Popen([probe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    children.append(child)
    assert select.select([child.stdout], [], [], 10)[0], 'backend did not initialize'
    assert child.stdout.readline().strip() == 'READY'
    wait({'name': 'FxSound'})
    return child

def stop(child, crash=False):
    if crash: child.kill()
    else:
        child.stdin.write('quit\n')
        child.stdin.flush()
    assert child.wait(timeout=5) == (-9 if crash else 0)

try:
    original = {'name': 'fxsound_test_output', 'test-extra': 'preserve exact JSON'}
    for initial in (original, None):
        set_default(initial)
        for _ in range(2): stop(start(), crash=True)
        stop(start())
        wait(initial)
        print('PASS: repeated crash/relaunch restores', initial, flush=True)
    for later in ({'name': 'fxsound_test_output'}, None):
        set_default(original)
        stop(start(), crash=True)
        set_default(later)
        stop(start())
        wait(later)
        print('PASS: crash recovery preserves later user choice', later, flush=True)
    set_default({'name': 'fxsound_test_output'})
    with tempfile.TemporaryDirectory(prefix='fx-recovery-unwritable-') as work:
        blocked = Path(work) / 'state-is-a-file'
        blocked.write_text('preserve this file')
        result = subprocess.run([probe], env=dict(os.environ, XDG_STATE_HOME=str(blocked)),
            input='quit\n', capture_output=True, text=True, timeout=10)
        assert result.returncode == 1 and 'INIT_FAILED' in result.stdout
        assert blocked.read_text() == 'preserve this file'
        wait({'name': 'fxsound_test_output'})
        print('PASS: recovery-state write failure leaves the default unchanged', flush=True)
finally:
    for child in children:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)
