#!/usr/bin/env bash
set -euo pipefail
command -v xvfb-run >/dev/null || exit 77
exec "$(dirname "$0")/run-isolated.sh" xvfb-run -a python3 "$(dirname "$0")/test_desktop_clients.py" "$1"
