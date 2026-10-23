# Public prerelease — v1.2.8.0-linux.1

Date: 2026-10-23. Maintainer: Lawliet2004.
Repository: https://github.com/Lawliet2004/FxSound_for_Linux.

## Source and licensing preparation

This is a clean snapshot of the locally hardened Linux port, based on the
upstream revision recorded in CREDITS.md. It preserves original file copyright
headers and the top-level AGPL license without claiming original authorship.
No upstream commit was rewritten. Upstream histories remain separately linked.

The inherited Gilroy files had no redistribution grant in the source tree and
were excluded from this repository's initial snapshot and all release assets.
Their generated embedded arrays were replaced with the unmodified Montserrat
Alternates Regular/SemiBold/Bold fonts, with SIL OFL texts and exact hashes.
Noto font license texts and retained embedded notices accompany the fonts.
Legacy Windows binary installer/driver payloads are also excluded. The actual
Linux DSP algorithms are retained. Font metrics can differ from Windows.

The Linux repository's own Git history contains only the clean snapshot and
subsequent commits. JUCE and Resources are pinned upstream gitlinks; the full
source archive additionally contains their corresponding source files.

See THIRD_PARTY_NOTICES.md for the licensing inventory and limits of this
review. Artwork, presets and community translations retain upstream provenance;
asset-specific trademark/permission clearance is not independently certified.
No guarantee of immunity from legal claims is made.

## Build and compatibility

Release, x86_64, Ubuntu 26.04.1, host glibc 2.43. PipeWire >=0.3.49 required.
The DEB's generated library dependencies are authoritative. The AppImage also
requires a sufficiently new host glibc; older Ubuntu/Kubuntu versions are not
claimed compatible. CI separately builds on Ubuntu 24.04; an unexecuted CI
configuration is not a verified result for that target.

Build uses the complete original JUCE GUI and real DSP/PipeWire backend, without
stub or sanitizer replacements. Locally extracted development dependencies
supply Ninja and XCB keysyms on this host. They are not committed as build tools.
AppImage packaging tooling bytes are checked against packaging/appimage-tools.sha256.

## Evidence

The release's VERIFICATION.md records executed tests and remaining gaps.
SHA256SUMS covers downloadable executable assets and the complete source archive.
The source SHA and tested executable hashes are recorded in release-manifest.json.
No host system installation or host audio default changes are required for tests.

Real compositor shortcut consent, physical-device latency/suspend/resume,
extended playback, full GUI/scaling, Windows build and waveform parity, default
PipeWire module-unload LeakSanitizer findings and certain abrupt-crash metadata
cases remain open. See KUBUNTU_PORT.md and IMPLEMENTATION_2026-10-23.md.
This is a prerelease, not a declaration that those gates are complete.
