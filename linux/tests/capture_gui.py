#!/usr/bin/env python3
"""Capture the original JUCE window in a disposable Xvfb/audio session.

Run through run-isolated.sh and xvfb-run; pass executable and output directory.
These captures document Linux rendering, not Windows pixel parity.
"""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

app = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
for language, view in [("en", "2"), ("en", "1"), ("zh-TW", "2")]:
    with tempfile.TemporaryDirectory(prefix="fxsound-gui-") as config:
        # Avoid the original first-version welcome path overriding --view.
        settings = Path(config) / "FxSound/FxSound.settings"
        settings.parent.mkdir()
        settings.write_text('<PROPERTIES><VALUE name="version" val="1.2.8.0"/></PROPERTIES>')
        with (output / f"{language}-view{view}.log").open("w") as log:
            process = subprocess.Popen(
                [str(app), f"--language={language}", f"--view={view}"],
                env=dict(os.environ, XDG_CONFIG_HOME=config), stdout=log, stderr=log,
                start_new_session=True)
            try:
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"FxSound exited with {process.returncode}")
                    metadata = subprocess.run(["pw-metadata", "0", "default.configured.audio.sink"],
                                              capture_output=True, text=True, check=True).stdout
                    if '"FxSound"' in metadata:
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError("FxSound did not initialize")
                time.sleep(0.5)  # Allow initial paints to reach the X server.
                subprocess.run(["ffmpeg", "-v", "error", "-f", "x11grab", "-video_size", "1280x800",
                                "-i", os.environ["DISPLAY"], "-frames:v", "1", "-y",
                                str(output / f"{language}-view{view}.png")], check=True)
            finally:
                # AppImage extraction mode may retain a launcher parent. Stop
                # the entire process group we created, not only that parent.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    metadata = subprocess.run(["pw-metadata", "0", "default.configured.audio.sink"],
                                              capture_output=True, text=True).stdout
                    if '\"FxSound\"' not in metadata:
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError("previous capture application did not release its private sink")
