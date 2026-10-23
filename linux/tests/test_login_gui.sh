#!/usr/bin/env bash
set -euo pipefail
[[ -f "$1" ]] || exit 77
command -v xvfb-run >/dev/null && command -v xwininfo >/dev/null || exit 77
exec "$(dirname "$0")/run-isolated.sh" xvfb-run -a -s '-screen 0 1280x800x24' python3 "$(dirname "$0")/test_login_gui.py" "$1" "$2"
