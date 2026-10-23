#!/usr/bin/env bash
set -euo pipefail
command -v xvfb-run >/dev/null || exit 77
exec xvfb-run -a python3 "$(dirname "$0")/test_server_restart.py" "$@"
