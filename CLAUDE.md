# FxSound for Linux — repository notes

This is Lawliet2004's unofficial Linux distribution. Read README.md, CREDITS.md,
THIRD_PARTY_NOTICES.md and MODIFICATIONS.md before changing attribution, assets
or licenses. Original copyright headers must remain intact.

- Build: `cmake --preset linux-release && cmake --build --preset linux-release`.
- Unit tests: `ctest --preset linux-unit`.
- Audio/GUI tests: use linux/tests/run-isolated.sh or disposable containers.
  Never terminate a user's running FxSound to make tests pass.
- Real-time processing: no allocations, file I/O, blocking locks or callback
  control-state mutation. Preserve original DSP algorithms.
- JUCE and Resources are pinned submodules. Do not replace their gitlinks with
  unreviewed dependency updates.
- Bundled fonts must have redistribution licenses and matching notices.
  packaging/verify-release-source.py checks generated font arrays and notices.
- This repository began as an attributed Linux source snapshot; upstream
  contributor histories are linked in CREDITS.md and must not be misrepresented.
- Changes to shared Windows sources need platform guards where applicable;
  explicitly document that Windows parity/build checks remain unverified.
- Use conventional lowercase commits. Do not rewrite published history.
- Pushes/releases to Lawliet2004/FxSound_for_Linux require user authorization;
  never push to the original FxSound or Linux-port contributors' repositories.
