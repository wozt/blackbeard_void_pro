#!/bin/bash
# Interactive installer for Blackbeard VOID PRO.
# Builds, then offers each step separately -- nothing is done behind your back.
set -uo pipefail
cd "$(dirname "$0")"

APP_ID="blackbeard-void-pro"
BIN="blackbeard_void_pro"
PREFIX="${PREFIX:-$HOME/.local}"
UDEV_RULE=/etc/udev/rules.d/99-corsair-void.rules

say()  { printf '\n\033[1m%s\033[0m\n' "$*"; }
ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
ask() {   # ask "question" -> 0 for yes. Defaults to yes on Enter.
  local reply
  read -r -p "  $1 [Y/n] " reply
  [[ -z "$reply" || "$reply" =~ ^[YyOo] ]]
}

# ---------------------------------------------------------------- dependencies
say "Checking dependencies"
missing=()
for pkg in gtk+-3.0 ayatana-appindicator3-0.1 hidapi-hidraw; do
  pkg-config --exists "$pkg" || missing+=("$pkg")
done
command -v pw-cli  >/dev/null || warn "pw-cli not found — PipeWire is required at runtime"
command -v pactl   >/dev/null || warn "pactl not found — PipeWire/PulseAudio tools are required"
if [ ${#missing[@]} -gt 0 ]; then
  warn "missing development packages: ${missing[*]}"
  echo "      On Debian/Ubuntu:"
  echo "        sudo apt install libgtk-3-dev libayatana-appindicator3-dev libhidapi-dev"
  exit 1
fi
ok "all build dependencies present"

# --------------------------------------------------------------------- build
say "Building"
if make -s; then
  ok "built ./$BIN"
else
  echo "  build failed" >&2
  exit 1
fi

# ------------------------------------------------------------------- udev rule
say "USB access"
if [ -f "$UDEV_RULE" ]; then
  ok "udev rule already installed"
else
  warn "without a udev rule, talking to the headset needs root"
  if ask "Install the udev rule now (needs sudo)?"; then
    sudo tee "$UDEV_RULE" >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTR{idVendor}=="1b1c", ATTR{idProduct}=="0a14", MODE="0660", GROUP="plugdev"
KERNEL=="hidraw*", ATTRS{idVendor}=="1b1c", ATTRS{idProduct}=="0a14", MODE="0660", GROUP="plugdev"
RULE
    sudo udevadm control --reload-rules && sudo udevadm trigger
    ok "rule installed — replug the dongle if it is already connected"
  else
    warn "skipped"
  fi
fi

# ---------------------------------------------------------------- install copy
say "Install location"
echo "  The binary can stay here and run from the build directory, or be"
echo "  installed into $PREFIX/bin (with its filters in $PREFIX/share)."
TARGET="$PWD/$BIN"
if ask "Install into $PREFIX?"; then
  make -s install PREFIX="$PREFIX"
  TARGET="$PREFIX/bin/$BIN"
  ok "installed to $TARGET"
  case ":$PATH:" in
    *":$PREFIX/bin:"*) : ;;
    *) warn "$PREFIX/bin is not in your PATH" ;;
  esac
else
  ok "running from $TARGET"
fi

# ----------------------------------------------------------------- menu entry
say "Menu entry"
APPDIR="$HOME/.local/share/applications"
mkdir -p "$APPDIR"
cat > "$APPDIR/$APP_ID.desktop" <<ENTRY
[Desktop Entry]
Type=Application
Name=Blackbeard VOID PRO
Comment=Corsair VOID PRO Wireless control
Exec=$TARGET
Icon=$APP_ID
Terminal=false
Categories=AudioVideo;Audio;Settings;
Keywords=corsair;headset;void;surround;equaliser;
StartupNotify=false
ENTRY
update-desktop-database "$APPDIR" 2>/dev/null
ok "added to the application menu"

# ------------------------------------------------------------------ autostart
say "Autostart"
AUTODIR="$HOME/.config/autostart"
if ask "Start automatically when you log in?"; then
  mkdir -p "$AUTODIR"
  HEADLESS=""
  if ask "Start hidden (tray icon only)?"; then HEADLESS=" --headless"; fi
  sed "s|^Exec=.*|Exec=$TARGET$HEADLESS|" "$APPDIR/$APP_ID.desktop" \
    > "$AUTODIR/$APP_ID.desktop"
  ok "autostart enabled${HEADLESS:+ (headless)}"
else
  rm -f "$AUTODIR/$APP_ID.desktop"
  ok "autostart left disabled"
fi

say "Done"
echo "  Launch it from your menu, or run: $TARGET"
echo "  The icon is published on first run."
