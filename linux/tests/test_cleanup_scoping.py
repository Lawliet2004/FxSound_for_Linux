#!/usr/bin/env python3
"""Private-session cleanup must never terminate an unrelated process or ancestor."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

helper=Path(__file__).with_name('cleanup_private_processes.py')
with tempfile.TemporaryDirectory(prefix='fxsound-isolated.cleanup-') as directory:
    root=Path(directory);(root/'run').mkdir()
    owned=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],env=dict(os.environ,XDG_RUNTIME_DIR=str(root/'run')))
    other=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'],env=dict(os.environ,XDG_RUNTIME_DIR=str(root/'other')))
    try:
        subprocess.run([sys.executable,str(helper),str(root)],check=True,timeout=5)
        assert owned.wait(timeout=3)!=0,'owned child survived cleanup'
        assert other.poll() is None,'unrelated process was terminated'
        print('PASS: private cleanup terminates only its owned runtime processes')
    finally:
        for process in (owned,other):
            if process.poll() is None:process.terminate()
            process.wait(timeout=3)
