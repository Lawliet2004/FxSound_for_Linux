#!/usr/bin/env python3
"""Regression: JUCE settings, DSP settings and presets respect XDG_CONFIG_HOME."""
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

legacy = Path.home() / "FxSound/FxSound.settings"
before = fingerprint(legacy)
with tempfile.TemporaryDirectory(prefix="fxsound-xdg-") as directory:
    config = Path(directory) / "café"
    config.mkdir()
    process = subprocess.Popen([str(Path(sys.argv[1]).resolve()), "--language=zh-TW", "--view=1"],
                               env=dict(os.environ, XDG_CONFIG_HOME=str(config)),
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("FxSound exited before initialization")
            if (config / "fxsound/registry.ini").is_file() and (config / "FxSound/Presets").is_dir():
                break
            time.sleep(0.1)
        else:
            raise RuntimeError("Settings/preset directories were not created under XDG_CONFIG_HOME")
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError("FxSound did not shut down cleanly")
    values = {item.attrib["name"]: item.attrib.get("val") for item in
              ET.parse(config / "FxSound/FxSound.settings").getroot().findall("VALUE")}
    assert values.get("language") == "zh-TW", values.get("language")
    assert values.get("view") == "1", values.get("view")
assert fingerprint(legacy) == before, "Legacy home-directory settings were modified"
print("PASS: GUI/DSP settings and presets use the UTF-8 XDG config directory; legacy settings untouched")
