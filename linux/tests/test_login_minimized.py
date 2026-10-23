#!/usr/bin/env python3
"""First-run login launches stay in the tray, even when the welcome path runs."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

app = str(Path(sys.argv[1]).resolve())
for login in (False, True):
    with tempfile.TemporaryDirectory(prefix='fx-login-') as config:
        with open(Path(config)/'app.log','w') as log:
            process = subprocess.Popen([app]+(['--autostart'] if login else []),
                env=dict(os.environ,XDG_CONFIG_HOME=config),stdout=log,stderr=log)
        try:
            deadline = time.monotonic()+10
            while time.monotonic()<deadline:
                assert process.poll() is None, 'app exited during initialization'
                metadata = subprocess.run(['pw-metadata','0','default.configured.audio.sink'],capture_output=True,text=True).stdout
                if '"FxSound"' in metadata: break
                time.sleep(.1)
            else: raise AssertionError('application not initialized')
            time.sleep(.4)
            tree = subprocess.check_output(['xwininfo','-root','-tree'],text=True)
            ids = re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+"FxSound"',tree,re.M)
            mapped=[]
            for window in ids:
                state=subprocess.check_output(['xwininfo','-id',window],text=True)
                if 'Map State: IsViewable' in state: mapped.append(window)
            assert bool(mapped) != login, f'login={login}: visible windows {mapped}\n{tree}'
            print('PASS:', 'login launch stays minimized' if login else 'normal first launch shows original window',flush=True)
            if login:
                for arguments,visible in [(['--autostart'],False),([],True)]:
                    with open(Path(config)/'duplicate.log','a') as log:
                        duplicate=subprocess.Popen([app]+arguments,env=dict(os.environ,XDG_CONFIG_HOME=config),stdout=log,stderr=log)
                    assert duplicate.wait(timeout=8)==0
                    time.sleep(.4)  # Let the first instance handle the broadcast.
                    deadline=time.monotonic()+3
                    while time.monotonic()<deadline:
                        tree=subprocess.check_output(['xwininfo','-root','-tree'],text=True)
                        windows=re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+"FxSound"',tree,re.M)
                        shown=any('Map State: IsViewable' in subprocess.check_output(['xwininfo','-id',window],text=True) for window in windows)
                        if shown==visible:break
                        time.sleep(.1)
                    assert shown==visible, f'duplicate {arguments} unexpectedly changed window visibility'
                print('PASS: duplicate login remains hidden; manual second launch opens existing window',flush=True)

        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=10)
