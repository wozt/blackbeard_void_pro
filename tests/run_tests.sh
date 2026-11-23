#!/usr/bin/env bash
# BVP_TESTS_SAFE_V1 -- default checks never interact with live audio or HID.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

usage() {
    echo "Usage: ./tests/run_tests.sh [safe|build|assets|guard|filters|config|desktop]" >&2
    echo "Live diagnostics: ./tests/run_tests_live.sh audio --i-accept-audio-interruption" >&2
}

mode="${1:-safe}"
case "$mode" in
    safe|build|assets|guard|filters|config|desktop) ;;
    audio|device|cleanup|all|live)
        echo "REFUSED: '$mode' is not part of the safe suite." >&2
        usage
        exit 2 ;;
    *) usage; exit 2 ;;
esac

if [[ "$mode" == safe || "$mode" == build ]]; then
    echo '[SAFE] Building application (no execution)'
    make -s all
fi
if [[ "$mode" == safe || "$mode" == assets ]]; then
    echo '[SAFE] Validating bundled JSON assets'
    python3 -m unittest discover -s tests -p 'test_wozt_ui_assets.py' -v
fi
if [[ "$mode" == safe || "$mode" == guard ]]; then
    echo '[SAFE] Checking test runner safety guards'
    python3 -m unittest discover -s tests -p 'test_runner_safety.py' -v
fi
if [[ "$mode" == safe || "$mode" == filters ]]; then
    echo '[SAFE] Checking impulse-response files (read-only)'
    for generation in v1 v2 v3; do
        for name in ir_LL ir_LR ir_RL ir_RR; do
            [[ -s "filters/$generation/$name.wav" ]] || {
                echo "ERROR: missing or empty filters/$generation/$name.wav" >&2
                exit 1
            }
        done
    done
fi
if [[ "$mode" == config ]]; then
    echo '[SAFE] Inspecting existing settings (read-only)'
    cfg="${XDG_CONFIG_HOME:-$HOME/.config}/blackbeard_void_pro/config"
    if [[ -f "$cfg" ]]; then
        python3 - "$cfg" <<'PYCODE'
from pathlib import Path
import sys
p = Path(sys.argv[1])
lines = p.read_text(encoding='utf-8').splitlines()
malformed = [i for i, line in enumerate(lines, 1)
             if line.strip() and not line.lstrip().startswith('#') and '=' not in line]
if malformed:
    raise SystemExit(f'Malformed settings on lines: {malformed}')
print('Config file syntax OK (not modified)')
PYCODE
    else
        echo '[SAFE] Settings file absent (normal on a fresh install)'
    fi
fi
if [[ "$mode" == desktop ]]; then
    echo '[SAFE] Inspecting desktop installation (read-only)'
    entry="${XDG_DATA_HOME:-$HOME/.local/share}/applications/blackbeard-void-pro.desktop"
    if [[ -f "$entry" ]]; then
        grep -q '^Icon=blackbeard-void-pro$' "$entry" || {
            echo 'Desktop icon identity mismatch' >&2; exit 1;
        }
        echo 'Desktop identity OK'
    else
        echo 'Desktop entry not installed; no action required'
    fi
fi
printf '[SAFE] OK: no live audio session, tray instance, default sink, or HID device was touched.\n'
