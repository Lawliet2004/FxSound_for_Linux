#!/usr/bin/env bash
# Run inside a disposable Ubuntu container with this source mounted read-only.
set -euo pipefail
[[ -f /.dockerenv ]] || { echo "Container-only test" >&2; exit 77; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
if ! command -v xvfb-run >/dev/null || ! command -v pactl >/dev/null || ! command -v xwininfo >/dev/null || ! command -v busctl >/dev/null; then
    apt-get install -y --no-install-recommends python3 xvfb xauth x11-utils dbus-x11 libglib2.0-bin procps systemd pipewire-bin pipewire-pulse pulseaudio-utils wireplumber
fi
bash /work/packaging/install.sh --yes /work/dist/fxsound_1.2.8.0+linux1_amd64.deb
cat /etc/os-release
ldd /usr/bin/fxsound
if ldd /usr/bin/fxsound | grep -q 'not found'; then exit 1; fi
/work/linux/tests/run-isolated.sh bash /work/linux/tests/test_xdg_config.sh /usr/bin/fxsound
/work/linux/tests/run-isolated.sh bash /work/linux/tests/test_login_minimized.sh /usr/bin/fxsound
/work/linux/tests/run-isolated.sh bash /work/linux/tests/test_server_restart.sh /usr/bin/fxsound --late-start --bypass
/work/linux/tests/run-isolated.sh xvfb-run -a python3 /work/linux/tests/fxtest.py clean-container /usr/bin/fxsound
export XDG_CONFIG_HOME=/tmp/fxsound-uninstall-test
mkdir -p "$XDG_CONFIG_HOME/autostart" "$XDG_CONFIG_HOME/FxSound/Presets"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=FxSound' 'Exec="/usr/bin/fxsound" --autostart' 'TryExec=/usr/bin/fxsound' 'X-FxSound-LaunchKind=native' > "$XDG_CONFIG_HOME/autostart/fxsound.desktop"
printf '%s\n' 'preserve-user-preset' > "$XDG_CONFIG_HOME/FxSound/Presets/user.fac"
bash /work/packaging/uninstall.sh --yes
[[ ! -e /usr/bin/fxsound && ! -e /usr/share/applications/fxsound.desktop && ! -e /usr/share/fxsound ]]
[[ ! -e "$XDG_CONFIG_HOME/autostart/fxsound.desktop" ]]
[[ $(cat "$XDG_CONFIG_HOME/FxSound/Presets/user.fac") == preserve-user-preset ]]
echo 'CLEAN CONTAINER INSTALL/RUNTIME/UNINSTALL: PASS'
