# Third-party notices

The top-level LICENSE applies to the FxSound application distribution; it does
not erase third-party copyright notices or independently license trademarks.

## FxSound and Linux-port sources

FxSound LLC and contributors: https://github.com/fxsound2/fxsound-app.
GNU Affero General Public License version 3; see LICENSE and retained file
headers. Some inherited files carry GPL v3-or-later notices; those notices
remain intact. Linux-port/hardening provenance is recorded in CREDITS.md.
The AGPL/GPL combined-work provisions apply where applicable; this distribution
does not relicense independently licensed dependencies under a new license.

FxSound artwork, icons, embedded translations and factory presets are retained
from the upstream source distribution. Attribution is preserved; no ownership,
endorsement or trademark permission is claimed. Resources remains a pinned
upstream submodule. No additional license grant is invented for individual
community contributions. Asset-specific permissions beyond the upstream
source distribution have not been independently certified.

## JUCE 6.1.6

Raw Material Software Limited and JUCE contributors.
https://github.com/juce-framework/JUCE
Pinned commit: 2f980209cc4091a4490bb1bafc5d530f16834e58.

The open-source distribution uses JUCE's GPL v3 licensing option, with the
module-specific ISC exceptions and embedded component notices preserved.
See third_party/JUCE/LICENSE.md, individual module headers, and
LICENSES/GPL-3.0.txt. The release full-source archive includes JUCE's source.
Commercial JUCE licenses are not claimed. The upstream README's statement
that JUCE itself is AGPL is not used as the licensing basis for this release.

## Fonts

All fonts in this snapshot are unmodified OFL fonts. They are distributed with
the application, not sold separately. The original Gilroy files are omitted;
Montserrat Alternates replaces them in source and generated embedded resources.

- Montserrat Alternates Regular, SemiBold, Bold: Copyright 2011 The Montserrat
  Project Authors; designers and current notice in LICENSES/MontserratAlternates-OFL.txt.
  Downloaded unmodified from https://github.com/google/fonts/tree/main/ofl/montserratalternates.
- Noto Sans KR/SC/TC: Copyright 2014–2020 Adobe; original embedded copyright
  notices retained. SIL OFL 1.1, LICENSES/NotoCJK-OFL.txt.
  Source project: https://github.com/notofonts/noto-cjk.
- Noto Sans Arabic: Copyright 2015–2020 Google LLC (embedded font notices);
  current upstream Noto Project Authors notice also retained in
  LICENSES/NotoArabic-OFL.txt. SIL OFL 1.1.
  https://github.com/notofonts/arabic.
- Noto Sans Thai: Copyright 2022 The Noto Project Authors;
  LICENSES/NotoThai-OFL.txt. SIL OFL 1.1.
  https://github.com/notofonts/thai.

Font copyright and license metadata remain embedded in every font binary.
packaging/font-sha256.txt records the exact redistributed font bytes.

## System and bundled libraries

PipeWire, WirePlumber, GLib/GIO, X11/XCB, Fontconfig, FreeType, ALSA, OpenGL and
the C/C++ runtime retain their own licenses. Native packages use the system
package manager's libraries. The AppImage builder copies available Debian
copyright notices for bundled libraries under
usr/share/doc/fxsound/bundled-libraries. System components excluded by the
AppImage tooling are supplied by the user's distribution.

## Exclusions and limitations

No Windows driver is linked into Linux. Legacy Windows driver/installer binary
payloads are excluded from this source snapshot and Linux release assets.
Credits and license texts are compliance measures, not legal clearance or a
warranty that all historical third-party permissions have been independently
established. Requests from rights holders should be raised with the maintainer
through this repository's issues.
