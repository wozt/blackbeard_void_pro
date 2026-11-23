#!/usr/bin/env bash
# BVP_LIVE_TESTS_V1 -- explicitly opt-in, hardware-sensitive diagnostics.
# It never kills Blackbeard instances or unrelated pw-cli processes.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

consent='--i-accept-audio-interruption'
if [[ $# -ne 2 || "$2" != "$consent" ]]; then
    echo "REFUSED: live tests may interrupt your active sound." >&2
    echo "Usage: ./tests/run_tests_live.sh [audio|device] $consent" >&2
    exit 2
fi
case "$1" in audio|device) group="$1" ;; *) echo "Unknown live group" >&2; exit 2 ;; esac

# Never run live diagnostics alongside the actual headset controller.
if pgrep -f '(^|[[:space:]])[^[:space:]]*/blackbeard_void_pro([[:space:]]|$)' >/dev/null 2>&1; then
    echo 'REFUSED: Blackbeard is running. Quit it from the tray first.' >&2
    exit 2
fi
if ! command -v pactl >/dev/null 2>&1; then
    echo 'REFUSED: pactl is unavailable.' >&2
    exit 2
fi
if pactl list short sinks 2>/dev/null | awk '{print $2}' | grep -qx 'blackbeard_in'; then
    echo 'REFUSED: Blackbeard virtual sink already exists; preserving the live audio chain.' >&2
    exit 2
fi
if [[ "$group" == device ]]; then
    echo '[LIVE] Read-only device discovery; no hidraw commands are sent.'
    lsusb | grep -i '1b1c:0a14' || { echo 'Corsair USB dongle not detected'; exit 1; }
    pactl list short cards | grep -i corsair || { echo 'Corsair sound card not detected'; exit 1; }
    echo '[LIVE] Device discovery OK.'
    exit 0
fi

# Audio tests create a dedicated null sink and test-owned probe processes.
# Do not run them from make test. Restore the original default even on errors.
command -v paplay >/dev/null || { echo 'paplay missing'; exit 1; }
command -v parec >/dev/null || { echo 'parec missing'; exit 1; }
make -s tests/chain_probe
original_default="$(pactl get-default-sink 2>/dev/null || true)"
[[ -n "$original_default" ]] || { echo 'No default sink to restore; aborting.' >&2; exit 1; }
null_module=''
probe_pid=''
rec_pid=''
test_sink="bvp_test_$$_$RANDOM"
tmpdir="$(mktemp -d -t bvp-live-XXXXXXXX)"

restore_streams() {
    # Re-home any applications auto-routed to our temporary Blackbeard sink.
    # Only the temporary sink is inspected, never unrelated application sinks.
    local virtual_id
    virtual_id="$(pactl list short sinks 2>/dev/null | awk '$2=="blackbeard_in" {print $1;exit}')"
    if [[ -n "$virtual_id" ]]; then
        while read -r input_id sink_id ignored; do
            if [[ "$sink_id" == "$virtual_id" ]]; then
                pactl move-sink-input "$input_id" "$original_default" >/dev/null 2>&1 || true
            fi
        done < <(pactl list short sink-inputs 2>/dev/null)
    fi
}
cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    if [[ -n "$rec_pid" ]]; then kill "$rec_pid" 2>/dev/null || true; wait "$rec_pid" 2>/dev/null || true; fi
    restore_streams
    pactl set-default-sink "$original_default" >/dev/null 2>&1 || true
    if [[ -n "$probe_pid" ]]; then kill "$probe_pid" 2>/dev/null || true; wait "$probe_pid" 2>/dev/null || true; fi
    if [[ -n "$null_module" ]]; then pactl unload-module "$null_module" >/dev/null 2>&1 || true; fi
    rm -rf -- "$tmpdir"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

null_module="$(pactl load-module module-null-sink "sink_name=$test_sink")"
[[ "$null_module" =~ ^[0-9]+$ ]] || { echo 'Null sink creation failed' >&2; exit 1; }

echo '[LIVE] Running convolution smoke tests against a disposable null sink'
for mode in 0 1 2; do
    echo "[LIVE] Convolution $((mode + 1)).0"
    BVP_TARGET_SINK="$test_sink" ./tests/chain_probe "$mode" >"$tmpdir/probe_$mode.log" 2>&1 &
    probe_pid=$!
    ready=0
    for attempt in {1..25}; do
        if ! kill -0 "$probe_pid" 2>/dev/null; then break; fi
        if pactl list short sinks 2>/dev/null | awk '{print $2}' | grep -qx 'blackbeard_in'; then ready=1; break; fi
        sleep 0.2
    done
    if ((ready != 1)); then
        echo "FAIL: convolution $((mode + 1)).0 did not create a sink." >&2
        sed -n '1,25p' "$tmpdir/probe_$mode.log" >&2
        exit 1
    fi
    probe_file='research/probe_src.wav'
    if [[ -s "$probe_file" ]]; then
        capture="$tmpdir/capture_$mode.raw"
        parec --device="${test_sink}.monitor" --format=float32le --rate=48000 \
            --channels=2 --raw "$capture" >/dev/null 2>&1 &
        rec_pid=$!
        sleep 0.5
        paplay --device=blackbeard_in "$probe_file" >/dev/null 2>&1 || {
            echo 'FAIL: cannot inject probe audio' >&2; exit 1;
        }
        sleep 0.6
        kill "$rec_pid" 2>/dev/null || true; wait "$rec_pid" 2>/dev/null || true
        rec_pid=''
        python3 - "$capture" <<'PYCODE'
from pathlib import Path
import math, struct, sys
raw = Path(sys.argv[1]).read_bytes()
values = (sample[0] for sample in struct.iter_unpack('<f', raw[:len(raw)//4*4]))
sumsq, count = 0.0, 0
for value in values:
    if math.isfinite(value):
        sumsq += value * value
        count += 1
rms = math.sqrt(sumsq/count) if count else 0.0
print(f'    Captured RMS: {rms:.6f}')
if rms <= 0.0005:
    raise SystemExit('FAIL: the test output is silent')
PYCODE
    else
        echo '    SKIP: optional research/probe_src.wav not found; sink creation only'
    fi
    restore_streams
    pactl set-default-sink "$original_default" >/dev/null 2>&1 || true
    kill "$probe_pid" 2>/dev/null || true
    wait "$probe_pid" 2>/dev/null || true
    probe_pid=''
    for attempt in {1..25}; do
        if ! pactl list short sinks 2>/dev/null | awk '{print $2}' | grep -qx 'blackbeard_in'; then break; fi
        sleep 0.2
    done
    if pactl list short sinks 2>/dev/null | awk '{print $2}' | grep -qx 'blackbeard_in'; then
        echo 'FAIL: test-created virtual sink did not disappear; do not kill unrelated processes.' >&2
        exit 1
    fi
done
echo '[LIVE] Passed. Original default sink restored; no unrelated processes stopped.'
