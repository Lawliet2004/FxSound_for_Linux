#!/usr/bin/env python3
"""Stop only descendants carrying this test's unique private runtime directory."""
import os
from pathlib import Path
import signal
import sys
import time

runtime = str(Path(sys.argv[1])/'run')
assert 'fxsound-isolated.' in runtime and Path(runtime).is_dir()
ancestors=set()
pid=os.getpid()
while pid>0 and pid not in ancestors:
    ancestors.add(pid)
    try:
        status=Path(f'/proc/{pid}/status').read_text()
        pid=int(next(line.split()[1] for line in status.splitlines() if line.startswith('PPid:')))
    except (OSError,StopIteration):break

def belongs(pid):
    if pid in ancestors:return False
    try:
        fields=Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
        return os.fsencode('XDG_RUNTIME_DIR='+runtime) in fields
    except OSError:return False

owned=[int(path.name) for path in Path('/proc').iterdir() if path.name.isdigit() and belongs(int(path.name))]
for pid in owned:
    try:os.kill(pid,signal.SIGTERM)
    except ProcessLookupError:pass
# Allow normal portal/server shutdown; the caller separately reaps its children.
until=time.monotonic()+1
while time.monotonic()<until and any(belongs(pid) for pid in owned):time.sleep(.05)
for pid in owned:
    if belongs(pid):
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
