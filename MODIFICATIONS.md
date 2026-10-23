# Modified distribution notice — 2026-10-23

This is a modified community distribution of FxSound, not the original Windows
release. Original license/copyright headers remain applicable. Local Linux
modifications are distributed under the repository's GNU AGPL v3 license.

Changes relative to the Linux upstream checkpoint in CREDITS.md include:

- DSP control/audio ownership separation and bounded snapshot handoff.
- PipeWire graph reconciliation, service-recovery and default restoration.
- UTF-8 settings/path handling and XDG configuration isolation.
- Desktop autostart, duplicate activation and uninstall cleanup.
- Native Wayland GlobalShortcuts portal sessions and application registration.
- Real-time, sanitizer, graph, desktop-client and packaging regression fixtures.
- CMake presets, DEB/AppImage/source packaging and release verification.
- Replacement of Gilroy fonts with SIL OFL Montserrat Alternates in the current
  source and generated embedded resources, including project references.
- Exclusion of legacy Windows installer/driver executables and archives from
  this Linux snapshot; no Windows driver is used in the Linux build.

Font substitution changes text metrics. Windows build, screenshot and waveform
parity have not been verified. This distribution makes no claim of endorsement
or of being an official FxSound Linux product.
