# Second Linux release: v1.2.8.0-linux.2

Date: 2026-10-24. This is an unofficial FxSound community prerelease.
The upstream application/DSP version remains 1.2.8.0; Linux revision 2 is
shown in the application version and AppImage name. The DEB version is
`1.2.8.0+linux2`, which upgrades the first Linux package.

## Fixed bugs

- Tray title, icon, connection and menu state are now owned by the private
  GLib thread. Producers publish work through a mutex-protected queue;
  bounded batches allow D-Bus requests and shutdown to run during updates.
  Realtime audio never uses this queue or its mutex.
- The tray context/loop are constructed before the worker starts. Shutdown
  attaches a high-priority stop source, so a quit requested before loop startup
  cannot be lost. Pending asynchronous registrations are cancelled and drained
  before their callback userdata is destroyed.
- Tray registration follows StatusNotifierWatcher ownership. A late desktop
  host or restarted panel gets a new asynchronous registration, with a bounded
  timeout and generation checks for stale replies.
- The original configured audio default is recorded before FxSound claims it.
  Private atomic recovery files under the XDG state directory are scoped to
  each audio-server endpoint. Repeated process crashes/relaunches preserve
  the exact original JSON, including an originally unset preference. A later
  user choice takes precedence. If state cannot be saved, FxSound leaves the
  existing default unchanged. Confirmed clean restoration removes the journal.

The original DSP algorithms, fonts, credits and licensing are preserved.
Windows version behavior is unchanged by the guarded Linux version string;
Windows builds and visual/audio parity were not verified.

## Regression coverage

`tray_lifecycle` checks 100 immediate component shutdowns, late tray-host
startup, host restart, concurrent property/menu updates and an unanswered
registration during shutdown. `crash_default_recovery` uses a private real
PipeWire/WirePlumber graph to check repeated crashes, exact/unset restoration,
later user preferences and recovery-state write failure.
`default_recovery_storage` checks atomic private files, server scoping,
malformed/oversized state and failed writes.

The new tray and crash regressions were first executed against the previous
implementation and failed with the reviewed defects. Release-specific results,
asset hashes and verification logs are shipped with the GitHub release.

System GLib/GIO reports occur in the full unsuppressed tray ThreadSanitizer
probes on this host. The previously reproduced application-owned title/icon
races no longer appear; this is not a claim that those full probes are free
of sanitizer warnings. The native protocol/stress regression and ASan tests
provide separate coverage. DSP TSan testing remains a separate check.

## Remaining acceptance limits

The binaries require x86_64 Linux, glibc >=2.43, PipeWire >=0.3.49 and
WirePlumber. AppImage testing uses extraction mode. Private audio tests disable
realtime-portal scheduling, so they do not establish physical latency/XRuns.
Actual hardware unplug/replug, Bluetooth transitions, suspend/resume, hours-long
playback, full GUI/scaling, compositor shortcut consent and Windows parity
remain unverified. Existing system-library/module-unloading leak findings and
unexercised metadata-owner fault cases are not declared fixed by this release.
