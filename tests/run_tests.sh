#!/bin/bash
# Standard test battery for Blackbeard VOID PRO.
#
# Audio tests route the chain into a null sink and measure the recorded
# level, so nothing is played into the headset. Device tests need the
# dongle on this host; they are skipped, not failed, when it is absent.
#
#   ./tests/run_tests.sh          run everything
#   ./tests/run_tests.sh audio    run one group (build|device|audio|config|desktop|cleanup)
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
BIN=$ROOT/blackbeard_void_pro
GROUP=${1:-all}

pass=0; fail=0; skip=0
ok()   { printf '  \033[32mPASS\033[0m %s\n' "$*"; pass=$((pass+1)); }
ko()   { printf '  \033[31mFAIL\033[0m %s\n' "$*"; fail=$((fail+1)); }
sk()   { printf '  \033[33mSKIP\033[0m %s\n' "$*"; skip=$((skip+1)); }
grp()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
want() { [ "$GROUP" = all ] || [ "$GROUP" = "$1" ]; }

kill_app()   { for p in $(pgrep -f "/blackbeard_void_pro$" 2>/dev/null); do kill "$p" 2>/dev/null; done; }
kill_chain() { for p in $(pgrep -x pw-cli 2>/dev/null); do kill "$p" 2>/dev/null; done; }
sinks()      { pactl list sinks short 2>/dev/null | grep -c blackbeard; }
dongle()     { lsusb 2>/dev/null | grep -q 1b1c:0a14 && pactl list cards short 2>/dev/null | grep -qi corsair; }

# ---------------------------------------------------------------- build
if want build; then
grp "Build"
  if make -s 2>/dev/null; then ok "compiles without error"; else ko "compilation failed"; fi
  [ -x "$BIN" ] && ok "binary present" || ko "binary missing"
  miss=0
  for v in v1 v2 v3; do
    for f in ir_LL ir_LR ir_RL ir_RR; do
      [ -f "filters/$v/$f.wav" ] || miss=$((miss+1))
    done
  done
  [ $miss -eq 0 ] && ok "all three filter generations present" \
                  || ko "$miss filter file(s) missing"

  # The installed binary resolves its filters from ~/.local/share, not from
  # the build tree: a filter present here but missing there makes the chain
  # load and then output silence, with no error anywhere.
  INST=$HOME/.local/share/blackbeard_void_pro/filters
  if [ -x "$HOME/.local/bin/blackbeard_void_pro" ]; then
    imiss=0
    for v in v1 v2 v3; do
      for f in ir_LL ir_LR ir_RL ir_RR; do
        [ -f "$INST/$v/$f.wav" ] || imiss=$((imiss+1))
      done
    done
    [ $imiss -eq 0 ] && ok "installed copy has all three generations" \
                     || ko "installed copy is missing $imiss filter file(s)"

    # The suite otherwise only exercises the source tree, which is how a
    # broken installed copy slipped through twice: check that the installed
    # binary actually resolves its filters and inserts the convolvers.
    # a running instance would make this launch a secondary one, which
    # exits at once without ever building a graph
    kill_app; sleep 1
    # the graph is one long line, so count occurrences, not lines
    conv=$(BVP_DEBUG=1 timeout 8 "$HOME/.local/bin/blackbeard_void_pro" 2>&1 >/dev/null \
           | grep -o "label = convolver" | wc -l)
    for p in $(pgrep -x pw-cli 2>/dev/null); do kill "$p" 2>/dev/null; done
    [ "${conv:-0}" -ge 4 ] && ok "installed binary loads its filters ($conv convolvers)" \
                           || ko "installed binary inserts no convolver: filters not found"
  else
    sk "not installed under ~/.local"
  fi
fi

# --------------------------------------------------------------- device
if want device; then
grp "Device (HID)"
  if ! dongle; then
    sk "dongle not on this host — device tests skipped"
  else
    ok "dongle detected"
    out=$(cat <<'PY' | python3 2>/dev/null
import os,time
try: fd=os.open("/dev/hidraw0", os.O_RDWR|os.O_NONBLOCK)
except Exception as e: print("ERR",e); raise SystemExit
os.write(fd, bytes([0xC9,0x64])); time.sleep(0.25)
for _ in range(20):
    try:
        d=os.read(fd,64)
        if len(d)>=5: print("OK", d[2]&0x7F, d[4]); break
    except BlockingIOError: time.sleep(0.05)
else: print("ERR no reply")
os.close(fd)
PY
)
    case "$out" in
      OK*) set -- $out
           lvl=$2; st=$3
           if [ "$lvl" -ge 0 ] && [ "$lvl" -le 100 ]; then
             ok "battery reads $lvl% (state $st)"
           else ko "battery out of range: $lvl"; fi ;;
      *)   ko "battery query failed ($out)" ;;
    esac
    ls -l /dev/hidraw0 2>/dev/null | grep -q plugdev \
      && ok "hidraw accessible without root" || sk "udev rule not installed"
  fi
fi

# ---------------------------------------------------------------- audio
if want audio; then
grp "Audio chain (silent: routed to a null sink)"
  if ! dongle; then
    sk "dongle absent — no Corsair sink to target"
  else
    kill_app; kill_chain; sleep 1
    MOD=$(pactl load-module module-null-sink sink_name=bvptest 2>/dev/null)
    PROBE=$(ls research/probe_src.wav 2>/dev/null || echo "")
    for mode in 0 1 2; do
      name="convolution $((mode+1)).0"
      BVP_TARGET_SINK=bvptest "$ROOT/tests/chain_probe" "$mode" >/dev/null 2>&1 &
      cp=$!
      sleep 3
      if [ "$(sinks)" -ge 1 ]; then ok "$name: sink created"; else ko "$name: no sink"; fi
      if [ -n "$PROBE" ]; then
        parec --device=bvptest.monitor --format=float32le --rate=48000 \
              --channels=2 --file-format=wav /tmp/bvp_$mode.wav & rp=$!
        sleep 0.5
        paplay --device=blackbeard_in "$PROBE" >/dev/null 2>&1
        sleep 1; kill $rp 2>/dev/null; wait $rp 2>/dev/null
        lvl=$(python3 - "$mode" <<'PY' 2>/dev/null
import sys,struct,numpy as np
d=open(f"/tmp/bvp_{sys.argv[1]}.wav","rb").read(); i=12; a=np.zeros(1)
while i+8<=len(d):
    c=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
    if c==b'data': a=np.frombuffer(d[i+8:i+8+sz],dtype='<f4').astype(float); break
    i+=8+sz+(sz&1)
print(f"{np.sqrt((a**2).mean()) if a.size else 0:.6f}")
PY
)
        if [ -n "$lvl" ] && python3 -c "import sys; sys.exit(0 if float('$lvl')>0.0005 else 1)"; then
          ok "$name: audio flows (rms $lvl)"
        else
          ko "$name: silent output (rms ${lvl:-?})"
        fi
      else
        sk "$name: no probe file, level not measured"
      fi
      kill $cp 2>/dev/null; wait $cp 2>/dev/null; kill_chain; sleep 1
    done
    [ -n "${MOD:-}" ] && pactl unload-module "$MOD" 2>/dev/null
  fi
fi

# --------------------------------------------------------------- config
if want config; then
grp "Settings"
  CFG=$HOME/.config/blackbeard_void_pro/config
  if [ -f "$CFG" ]; then
    ok "config file exists"
    # one curve per method: eq<mode>_<band>
    tot=$(grep -cE '^eq[0-9]+_[0-9]+ = ' "$CFG")
    flat=$(grep -cE '^eq[0-9]+_[0-9]+ = 0$' "$CFG")
    if [ "$tot" -eq 30 ]; then
      ok "per-method equaliser curves stored ($tot bands)"
      [ "$flat" -eq 30 ] && ok "every equaliser is flat" \
        || sk "$((tot-flat)) band(s) not at zero"
    else
      ko "expected 30 equaliser entries, found $tot"
    fi
    grep -qE '^preamp[012] = ' "$CFG" && ok "per-method preamp stored" \
      || ko "per-method preamp missing"
    grep -qE '^dolby_mode = [012]$' "$CFG" && ok "dolby mode stored" || ko "dolby mode missing"
  else
    sk "no config yet (never run)"
  fi
fi

# -------------------------------------------------------------- desktop
if want desktop; then
grp "Desktop integration"
  [ -f "$HOME/.local/share/applications/blackbeard-void-pro.desktop" ] \
    && ok "menu entry present" || sk "menu entry absent"
  [ -f "$HOME/.local/share/icons/hicolor/256x256/apps/blackbeard-void-pro.png" ] \
    && ok "icon published" || sk "icon absent"
  if [ -f "$HOME/.config/autostart/blackbeard-void-pro.desktop" ]; then
    exe=$(grep -oP '^Exec=\K\S+' "$HOME/.config/autostart/blackbeard-void-pro.desktop")
    [ -x "$exe" ] && ok "autostart points at an existing binary" \
                  || ko "autostart Exec is broken: $exe"
  else
    sk "autostart disabled"
  fi
fi

# -------------------------------------------------------------- cleanup
if want cleanup; then
grp "Cleanup and single instance"
  if ! dongle; then
    sk "dongle absent"
  else
    kill_app; kill_chain; sleep 1
    "$BIN" >/dev/null 2>&1 & sleep 5
    n=$(pgrep -cf "/blackbeard_void_pro$")
    [ "$n" -eq 1 ] && ok "one instance running" || ko "$n instances"
    timeout 10 "$BIN" >/dev/null 2>&1
    n=$(pgrep -cf "/blackbeard_void_pro$")
    [ "$n" -eq 1 ] && ok "second launch does not duplicate" || ko "duplicated ($n)"
    for p in $(pgrep -f "/blackbeard_void_pro$"); do kill -9 "$p" 2>/dev/null; done
    sleep 2
    [ "$(sinks)" -eq 0 ] && ok "no ghost sink after kill -9" \
                         || ko "$(sinks) ghost sink(s) left"
  fi
fi

printf '\n\033[1mTotal\033[0m  %d passed, %d failed, %d skipped\n' $pass $fail $skip
[ $fail -eq 0 ]
