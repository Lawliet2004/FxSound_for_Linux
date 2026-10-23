# Release verification — v1.2.8.0-linux.1

Executed 2026-10-23 against the clean, attributed publication snapshot.

| Check | Result |
|---|---|
| Complete Release build, real JUCE GUI + DSP + PipeWire | PASS |
| Unit tests, including portal protocol and packaged font/resource integrity | 16/16 PASS |
| DEB in clean Ubuntu 26.04 container | Install/runtime/uninstall PASS |
| Native packaged playback/lifecycle suite | 36 PASS, 0 FAIL, 0 BLOCKED |
| Native UTF-8 XDG settings and login/duplicate activation | PASS |
| Native delayed audio startup in saved bypass, subsequent server/session-manager recovery | PASS |
| Native uninstall | User preset retained; matching autostart removed |
| AppImage playback/lifecycle suite in separate container | 36 PASS, 0 FAIL, 0 BLOCKED |
| Private PipeWire graph lifecycle, including live metadata clear-all | PASS |
| Desktop launcher validation | PASS |
| Packaged native executable equals tested Release executable | PASS |
| Packaged notices/OFL/GPL texts and font bytes | PASS |
| All committed history scanned for excluded inherited fonts/Windows binary payload names | No matches |
| Common private-key/token-pattern scan of snapshot files | No matches; not a formal security audit |
| Top-level LICENSE hash versus upstream checkout | Identical |

The DEB playback sample measured 4.5% of one CPU core, 43 MiB RSS. The AppImage
sample measured 4.4%, 39 MiB RSS. Both sampled 60-second synthetic playback
runs passed their processor error check. These are private null-output tests,
not physical speaker/headphone latency or long-running XRuns measurements.

Tests use disposable containers, isolated PipeWire/D-Bus sessions and Xvfb.
`DISABLE_RTKIT=1` avoids the host realtime-portal startup boundary; this does not
prove realtime scheduling. AppImages ran with `APPIMAGE_EXTRACT_AND_RUN=1`;
ordinary FUSE mounting is unverified. The user's running FxSound instance was
left untouched; host application tests that detected it were skipped and were
replaced by container tests.

The initial screenshot helper did not terminate an extracted AppImage's child
process, making later captures repeat the first window. The fixture now creates
and terminates its own process group and waits for private-default restoration.
Only corrected captures should be used as release evidence. These captures do
not establish Windows pixel parity or complete GUI interaction coverage.

## Remaining limitations

This prerelease requires x86_64 Linux, glibc >=2.43, PipeWire >=0.3.49 and
WirePlumber; native dependencies are also declared in the DEB. Older Ubuntu /
Kubuntu releases are not covered by this binary build. Wayland compositor
permission/rebinding UI, physical audio devices, sleep/resume, extended playback,
full controls/themes/scales, Windows build/audio/visual parity and some abrupt
crash/default-metadata cases remain unverified. The previously isolated
PipeWire module-unloading LeakSanitizer boundary remains unresolved.

A full source archive accompanies the binary assets, including pinned JUCE and
Resources source. GitHub's automatic source downloads omit submodule contents;
use the full archive or clone recursively. SHA256SUMS and release-manifest.json
identify the uploaded assets and source commit. Credits and license preservation
are documented in CREDITS.md and THIRD_PARTY_NOTICES.md; they are not a guarantee
of legal clearance for every inherited asset or trademark use.
