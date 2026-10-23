#!/usr/bin/env bash
# apt checks dependencies; no ad-hoc copies into system directories.
set -euo pipefail
apt_options=()
if [[ ${1:-} == --yes ]]; then apt_options=(--yes); shift; fi
if [[ $# != 1 || ! -f "$1" || "$1" != *.deb ]]; then
    echo "Usage: $0 [--yes] /path/to/fxsound.deb" >&2
    exit 2
fi
package_path="$(realpath -- "$1")"
[[ "$(dpkg-deb -f "$package_path" Package)" == fxsound ]] || {
    echo "The package is not fxsound" >&2; exit 2;
}
if (( EUID == 0 )); then
    apt-get install "${apt_options[@]}" -- "$package_path"
else
    sudo apt-get install "${apt_options[@]}" -- "$package_path"
fi
