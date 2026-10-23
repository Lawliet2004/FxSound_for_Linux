#!/usr/bin/env python3
"""Check real Desktop Entry launches and user-scoped uninstall cleanup."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

probe = str(Path(sys.argv[1]).resolve())
repo = Path(__file__).resolve().parents[2]
module_spec = importlib.util.spec_from_file_location('cleanup', repo/'packaging/cleanup-autostart.py')
module = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(prefix='fx-auto-音響-') as temp:
    root = Path(temp)
    entry = root/'config/autostart/fxsound.desktop'
    result = root/'argv.json'
    settings = root/'config/FxSound/presets.fac'
    settings.parent.mkdir(parents=True)
    settings.write_text('preserve user presets')
    names = ['Fx Sound', 'Fx%U 100%', 'Fx$() `tick` "quote" \\slash', 'Fx=音響 🎧', 'Fx\nnew\tline']
    for name in names:
        executable = root/name
        executable.write_text('#!/usr/bin/python3\nimport os,sys,json\ntarget=os.environ["FX_AUTO_CAPTURE"];open(target+".tmp","w").write(json.dumps(sys.argv));os.replace(target+".tmp",target)\n')
        executable.chmod(0o700)
        subprocess.run([probe,'create',str(entry),'/tmp/.mount_fxsound/usr/bin/fxsound',str(executable),'/tmp/.mount_fxsound'],check=True)
        assert '/tmp/.mount_fxsound' not in entry.read_text()
        subprocess.run([probe,'enabled',str(entry)],check=True)
        subprocess.run([probe,'launch',str(entry)],env=dict(os.environ,FX_AUTO_CAPTURE=str(result)),check=True)
        for _ in range(100):
            if result.exists(): break
            time.sleep(.02)
        assert result.exists(), 'launcher did not run the persistent executable'
        assert json.loads(result.read_text()) == [str(executable),'--autostart']
        result.unlink()
        assert not module.cleanup(entry.parents[1], '/usr/bin/fxsound'), 'native uninstall removed an AppImage entry'
        print('PASS: Desktop Entry round trip', repr(name), flush=True)
    # No stable APPIMAGE is available: never save an AppDir mount as Exec.
    before = entry.read_bytes()
    assert subprocess.run([probe,'create',str(entry),'/tmp/.mount_fxsound/bin/fxsound','','/tmp/.mount_fxsound']).returncode != 0
    assert entry.read_bytes() == before
    subprocess.run([probe,'disable',str(entry)],check=True)
    assert not entry.exists()
    native = root/'native-fxsound'
    native.write_text('#!/bin/sh\nexit 0\n'); native.chmod(0o700)
    subprocess.run([probe,'create',str(entry),str(native),'',''],check=True)
    assert module.cleanup(entry.parents[1], str(native)) and not entry.exists()
    # Native launches may inherit AppImage variables from an unrelated parent.
    subprocess.run([probe,'create',str(entry),str(native),str(executable),'/tmp/unrelated.AppDir'],check=True)
    assert 'X-FxSound-LaunchKind=native' in entry.read_text()
    assert module.cleanup(entry.parents[1], str(native))
    for command,removed in [('/usr/bin/fxsound',True),('fxsound',True),('/opt/my.AppImage',False),('/usr/bin/fxsound --custom',False)]:
        entry.write_text('[Desktop Entry]\nType=Application\nName=FxSound\nExec='+command+'\n')
        assert module.cleanup(entry.parents[1], '/usr/bin/fxsound') == removed
    entry.write_text('[Desktop Entry]\nType=Application\nName=Other App\nExec=/usr/bin/fxsound\n')
    assert not module.cleanup(entry.parents[1], '/usr/bin/fxsound')
    subprocess.run([probe,'create',str(entry),str(native),'',''],check=True)
    entry.write_text(entry.read_text()+'Hidden=true\n')
    assert subprocess.run([probe,'enabled',str(entry)]).returncode != 0
    native.unlink()
    assert subprocess.run([probe,'enabled',str(entry)]).returncode != 0
    fakebin = root/'fake-bin'; fakebin.mkdir()
    for name,body in {
        'pgrep':'exit "${FX_FAKE_RUNNING:-1}"',
        'sudo':'exec "$@"',
        'apt-get':'echo "$*" >> "$FX_UNINSTALL_RECORD"; exit "${FX_APT_STATUS:-0}"',
    }.items():
        stub=fakebin/name;stub.write_text('#!/bin/sh\n'+body+'\n');stub.chmod(0o700)
    environment=dict(os.environ,PATH=str(fakebin)+os.pathsep+os.defpath,
        XDG_CONFIG_HOME=str(entry.parents[1]),FX_UNINSTALL_RECORD=str(root/'apt.log'))
    environment.pop('SUDO_USER',None)
    for status in ('23','0'):
        entry.write_text('[Desktop Entry]\nType=Application\nName=FxSound\nExec=/usr/bin/fxsound\n')
        result=subprocess.run(['bash',str(repo/'packaging/uninstall.sh')],env=dict(environment,FX_APT_STATUS=status),capture_output=True,text=True)
        assert result.returncode==int(status),result.stderr
        assert entry.exists()==(status!='0'), 'cleanup must follow successful apt removal'
    assert (root/'apt.log').read_text().splitlines()==['remove -- fxsound']*2
    assert settings.read_text() == 'preserve user presets'
    print('PASS: disabled/missing executable, stable-path refusal and scoped uninstall cleanup',flush=True)
