# FxSound for Linux — unofficial community port

Native Linux/PipeWire port of **[FxSound by FxSound LLC](https://www.fxsound.com/)**,
using the original JUCE interface, presets and DSP engine. This independent
community project is not an official FxSound release and is not endorsed by
FxSound LLC. FxSound's name and artwork identify the original application;
no trademark rights or endorsement are claimed.

## Download and launch

Get the x86_64 **AppImage** or **DEB** from
[Releases](https://github.com/Lawliet2004/FxSound_for_Linux/releases).
The second Linux release, **v1.2.8.0-linux.2**, fixes launcher/tray races and
restores the configured output after a crash and relaunch. It remains a
**prerelease**, built on Ubuntu 26.04.1; it requires
**glibc 2.43 or newer**, PipeWire and WirePlumber. The AppImage does not make
this build compatible with older Ubuntu/Kubuntu installations. Windows and
macOS are unsupported by these Linux packages.

```bash
chmod +x FxSound-1.2.8.0-linux.2-x86_64.AppImage
./FxSound-1.2.8.0-linux.2-x86_64.AppImage
# If FUSE is unavailable:
APPIMAGE_EXTRACT_AND_RUN=1 ./FxSound-1.2.8.0-linux.2-x86_64.AppImage
```

Or install the DEB with dependency resolution:

```bash
sudo apt install ./fxsound_1.2.8.0+linux2_amd64.deb
```

The DEB installs an application-menu launcher. Start FxSound, choose your
speakers/headphones as output, play audio, then enable processing. Close an
existing instance before launching another build. Wayland global shortcuts
require the compositor's GlobalShortcuts portal and its permission dialog.

Verify downloads with `sha256sum -c SHA256SUMS` in the download directory.

## Build from source

```bash
git clone --recurse-submodules --shallow-submodules https://github.com/Lawliet2004/FxSound_for_Linux.git
cd FxSound_for_Linux
sudo apt install build-essential cmake ninja-build pkg-config \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
  libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libasound2-dev \
  libglib2.0-dev libpipewire-0.3-dev libxcb1-dev libxcb-keysyms1-dev \
  python3 python3-gi dbus-daemon
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-unit
./build-release/FxSound
```

PipeWire development/runtime version must be at least 0.3.49. Building on an
older supported distribution can produce a different ABI baseline; those
builds are not covered by the initial binary release's validation.
The release's full source archive includes the pinned dependency sources;
GitHub's automatic source ZIP/tarballs do not include submodule contents.

## Credits and licensing

- **FxSound LLC and original contributors**: original application, DSP, UI,
  presets and assets — [fxsound2/fxsound-app](https://github.com/fxsound2/fxsound-app).
- **ealtun21 and Linux-port contributors**: initial PipeWire/Linux port —
  [ealtun21/fxsound-app](https://github.com/ealtun21/fxsound-app).
- **L0ic4 and hardening contributors**: Linux portability and regression fixes.
- **Theremino**: feature enhancements acknowledged by the upstream project.
- **JUCE contributors**, **Montserrat Alternates designers**, **Noto/Adobe/Google
  font contributors**, and translation/preset contributors: see
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- **Lawliet2004**: this community distribution and additional Linux integration.

The application is distributed under **GNU AGPL v3**, with original notices
retained. Dependency and font licenses remain separate. See [LICENSE](LICENSE),
[CREDITS.md](CREDITS.md), [LICENSES](LICENSES), and
[MODIFICATIONS.md](MODIFICATIONS.md). No claim of original authorship of
FxSound or its audio algorithms is made.

The repository's sponsorship link retains the original FxSound donation destination.

For distribution, this snapshot replaces the inherited Gilroy font binaries
with unmodified SIL OFL Montserrat Alternates fonts; text metrics therefore
can differ from the Windows UI. Windows installer/driver binaries are excluded.
The original contributor history remains at the linked upstream repositories;
this repository begins with a clean, attributed Linux source snapshot.

## Validation and limitations

Development checkpoint, not a finished or certified release. Physical-device
latency, suspend/resume, long playback, full UI/scaling, Windows visual/audio
parity and some crash-recovery scenarios remain unverified. See
[release verification](docs/development/PUBLIC_RELEASE.md) and
[remaining work](docs/development/IMPLEMENTATION_2026-10-23.md).
The second release's fixes, regression coverage and remaining limits are
recorded in [Linux release 2](docs/development/RELEASE_2.md).

Report Linux-port problems to [this repository's issues](https://github.com/Lawliet2004/FxSound_for_Linux/issues).
For the official Windows project, use the upstream links above.
