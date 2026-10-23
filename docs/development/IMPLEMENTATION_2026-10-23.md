# Implementation checkpoint — 2026-10-23

## Changes

- Native Wayland shortcuts use the GlobalShortcuts desktop portal, rather than
  XWayland grabs. A private GLib context and D-Bus connection keep consent off
  the JUCE message thread. Registrations are coalesced into one session;
  changing bindings closes the old session, and disabling closes requests and
  sessions. Activations are filtered by session and registration generation.
  Denial does not cause repeated prompts. The private connection registers the
  `fxsound` desktop application ID before portal calls. Portal owner changes
  retry sessions and register again.
  X11 retains its existing XCB backend.
- `_wfopen_s` explicitly encodes validated Unicode scalar values into UTF-8,
  independent of locale. It rejects malformed codepoints, null inputs and
  oversized modes, clears the output handle on failure and returns actual errno.
  Paths are dynamically sized rather than truncated to a fixed stack buffer.
- PipeWire subject-zero metadata clear-all invalidates cached default ownership;
  shutdown respects the cleared user preference. Individual configured-default
  deletion also releases the configured-device preference for future routing.
- Added private fake-portal protocol tests and UTF-8 file-opening tests, plus a
  real private PipeWire regression for metadata clear-all while processing.

## Verification

- Complete native GUI/DSP application build passes.
- Unit tests: 15/15 PASS.
- Private PipeWire tests: 7/7 PASS, including real GUI lifecycle tests under
  the project's local Xvfb and the new live clear-all regression.
- Portal fixture covers trigger encoding, activation, rebinding, disabling,
  invalid keys, denied permission, pending-request cancellation and shutdown.
  This is protocol coverage, not real compositor approval evidence.
- Server restart/delayed-login recovery: 3/3 PASS.
- Recovery tests require `DISABLE_RTKIT=1` in this synthetic test environment:
  without it, replacement PipeWire servers can block on the desktop realtime
  portal. This does not establish realtime scheduling or physical XRuns.
- Existing dist/manifest.json describes October 21 artifacts. New artifacts
  belong under dist/2026-10-23; do not apply the old validation to them.

## Outstanding acceptance and implementation work

This checkpoint is not a completed release. Hardware unplug/replug, physical
bypass/latency, actual suspend/resume, extended playback, Snap confinement,
complete UI controls/themes/scales and Windows visual/DSP/build parity remain
unverified. Windows checks require a Windows reference/toolchain. Physical
checks require suitable devices and a controlled interactive session.

Abrupt process death/persistent-default recovery, permission failures and hung
metadata owners still need fault-injection acceptance work. Default PipeWire
module-unloading LeakSanitizer reports remain unresolved as described in
KUBUNTU_PORT.md. Older Kubuntu compatibility requires an older build baseline;
the current host-built package does not establish it. Immutable archival of
AppImage build tooling remains open.
