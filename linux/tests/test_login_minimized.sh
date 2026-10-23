#!/usr/bin/env bash
set -euo pipefail
command -v xvfb-run >/dev/null && command -v xwininfo >/dev/null || exit 77
exec xvfb-run -a python3 "$(dirname "$0")/test_login_minimized.py" "$1"
