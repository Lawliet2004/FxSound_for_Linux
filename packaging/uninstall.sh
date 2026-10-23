#!/usr/bin/env bash
# Keep user presets/settings. The package manager removes only owned files.
set -euo pipefail
apt_options=()
if [[ ${1:-} == --yes ]]; then apt_options=(--yes); shift; fi
[[ $# == 0 ]] || { echo "Usage: $0 [--yes]" >&2; exit 2; }
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
command -v python3 >/dev/null || { echo "Python 3 is required to clean the login entry" >&2; exit 1; }
if pgrep -x fxsound >/dev/null || pgrep -x FxSound >/dev/null; then
    echo "Quit FxSound from its tray menu before uninstalling so it restores audio routing." >&2
    exit 1
fi
if (( EUID == 0 )); then
    apt-get remove "${apt_options[@]}" -- fxsound
else
    sudo apt-get remove "${apt_options[@]}" -- fxsound
fi

# Run as the account that requested removal. The package's TryExec makes stale
# entries in other accounts harmless; never sweep other users' configuration.
if (( EUID == 0 )) && [[ -n ${SUDO_USER:-} && $SUDO_USER != root ]]; then
    sudo -u "$SUDO_USER" -- python3 "$script_dir/cleanup-autostart.py"
else
    python3 "$script_dir/cleanup-autostart.py"
fi
