#!/usr/bin/env bash
# Run graph tests against private PipeWire/Pulse/WirePlumber and D-Bus servers.
# Hardware monitors are disabled: no host devices or defaults are changed.
# Usage: linux/tests/run-isolated.sh ctest --test-dir build -L pw-live -V
set -euo pipefail

if [[ ${1:-} != --session ]]; then
    # JUCE's single-instance lock spans displays and private audio sessions.
    # Never let graph tests redirect/terminate an already-running user instance.
    if pgrep -x FxSound >/dev/null || pgrep -x fxsound >/dev/null; then
        echo "FxSound is already running; isolated application tests skipped" >&2
        exit 77
    fi
    for tool in pipewire pipewire-pulse wireplumber dbus-run-session pactl pw-dump; do
        command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 77; }
    done
    [[ $# -gt 0 ]] || { echo "Usage: $0 command [args...]" >&2; exit 2; }
    test_root=$(mktemp -d "${TMPDIR:-/tmp}/fxsound-isolated.XXXXXXXX")
    mkdir -p "$test_root"/{run,config/wireplumber/wireplumber.conf.d,state,cache}
    chmod 700 "$test_root/run"
    cat > "$test_root/config/wireplumber/wireplumber.conf.d/99-test.conf" <<'CONF'
wireplumber.profiles = {
  main = {
    monitor.alsa = disabled
    monitor.alsa-midi = disabled
    monitor.bluez = disabled
    monitor.bluez-midi = disabled
    monitor.v4l2 = disabled
    monitor.libcamera = disabled
  }
}
CONF
    echo "Isolated audio logs: $test_root"
    # Keep D-Bus activation children from inheriting the test runner's output
    # pipe. Long-lived desktop services must not hold a completed CTest open.
    set +e
    env -u PIPEWIRE_REMOTE -u PIPEWIRE_CORE -u WIREPLUMBER_CONFIG_DIR \
        XDG_RUNTIME_DIR="$test_root/run" \
        PIPEWIRE_RUNTIME_DIR="$test_root/run" \
        XDG_CONFIG_HOME="$test_root/config" XDG_STATE_HOME="$test_root/state" \
        XDG_CACHE_HOME="$test_root/cache" PULSE_RUNTIME_PATH="$test_root/run/pulse" \
        PULSE_SERVER="unix:$test_root/run/pulse/native" \
        dbus-run-session -- "$0" --session "$test_root" "$@" >"$test_root/session.log" 2>&1
    result=$?
    set -e
    cat "$test_root/session.log"
    exit "$result"
fi

test_root=$2
shift 2
server_pids=()
cleanup() {
    python3 "$(dirname "$0")/cleanup_private_processes.py" "$test_root" || true
    for pid in "${server_pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    for pid in "${server_pids[@]}"; do wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT
pipewire >"$test_root/pipewire.log" 2>&1 & server_pids+=("$!")
# WirePlumber can exit immediately if launched before the core socket exists.
# Start each dependent server only after its prerequisite accepts connections.
core_ready=false
for ((attempt=0; attempt<100; ++attempt)); do
    kill -0 "${server_pids[0]}" 2>/dev/null || { echo "Private PipeWire exited" >&2; exit 1; }
    if [[ -S "$test_root/run/pipewire-0" ]]; then core_ready=true; break; fi
    sleep 0.05
done
[[ $core_ready == true ]] || { echo "Private PipeWire socket not ready" >&2; exit 1; }
pipewire-pulse >"$test_root/pulse.log" 2>&1 & server_pids+=("$!")
pulse_ready=false
for ((attempt=0; attempt<100; ++attempt)); do
    kill -0 "${server_pids[1]}" 2>/dev/null || { echo "Private Pulse server exited" >&2; exit 1; }
    if pactl info >/dev/null 2>&1; then pulse_ready=true; break; fi
    sleep 0.05
done
[[ $pulse_ready == true ]] || { echo "Private Pulse server not ready" >&2; exit 1; }
wireplumber >"$test_root/wireplumber.log" 2>&1 & server_pids+=("$!")

# Wait for both the pulse socket and WirePlumber's default metadata to exist.
ready=false
for ((attempt=0; attempt<100; ++attempt)); do
    for pid in "${server_pids[@]}"; do
        kill -0 "$pid" 2>/dev/null || { echo "Private audio server exited; see $test_root" >&2; exit 1; }
    done
    if pactl info >/dev/null 2>&1 && pw-dump | python3 -c '
import json, sys
sys.exit(not any(o.get("type") == "PipeWire:Interface:Metadata" and
    o.get("props", {}).get("metadata.name") == "default" for o in json.load(sys.stdin)))'; then
        ready=true
        break
    fi
    sleep 0.1
done
[[ $ready == true ]] || { echo "Private audio session did not become ready" >&2; exit 1; }
pactl load-module module-null-sink sink_name=fxsound_test_output >/dev/null
output_ready=false
for ((attempt=0; attempt<100; ++attempt)); do
    if pactl set-default-sink fxsound_test_output >/dev/null 2>&1; then output_ready=true; break; fi
    sleep 0.05
done
[[ $output_ready == true ]] || { echo "Private output did not become configurable" >&2; exit 1; }
pw-dump > "$test_root/initial-graph.json"
python3 - "$test_root/initial-graph.json" <<'PY'
import json, sys
objects = json.load(open(sys.argv[1]))
hardware = [o for o in objects if o.get("type") == "PipeWire:Interface:Device"]
if hardware:
    raise SystemExit("Hardware appeared in the isolated session; refusing to test")
PY
"$@"
