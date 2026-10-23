#!/usr/bin/env bash
set -euo pipefail
command -v xvfb-run >/dev/null && command -v python3 >/dev/null || exit 77
exec xvfb-run -a python3 "$(dirname "$0")/test_xdg_config.py" "$1"
