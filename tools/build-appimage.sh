#!/usr/bin/env bash
# Build a WOZT GTK Standard x86_64 AppImage from the current sources.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APPDIR="${APPDIR:-$ROOT/dist/AppDir}"
LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy}"
if [[ "$(uname -m)" != x86_64 ]]; then
  echo "This packaging recipe targets Linux x86_64" >&2
  exit 1
fi
for tool in make pkg-config cc "$LINUXDEPLOY"; do
  command -v "$tool" >/dev/null || { echo "Missing: $tool" >&2; exit 1; }
done
cd "$ROOT"
make -j"$(nproc)"
rm -rf "$APPDIR"
make install PREFIX="$APPDIR/usr"

# Produce the same Cairo icon as the running application, independent of
# whatever may already exist in the build machine's ~/.local/share.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/icon_main.c" <<'EOF'
#include "icons.h"
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    return bvp_write_boat_png(argv[1], 256) ? 0 : 1;
}
EOF
cc -I"$ROOT/src" $(pkg-config --cflags gtk+-3.0) \
  "$TMP/icon_main.c" "$ROOT/src/icons.c" \
  $(pkg-config --libs gtk+-3.0) -o "$TMP/make_icon"
ICON="$APPDIR/usr/share/icons/hicolor/256x256/apps/blackbeard-void-pro.png"
mkdir -p "$(dirname "$ICON")"
"$TMP/make_icon" "$ICON"
DESKTOP="$APPDIR/usr/share/applications/blackbeard-void-pro.desktop"
mkdir -p "$(dirname "$DESKTOP")"
cat > "$DESKTOP" <<'EOF'
[Desktop Entry]
Type=Application
Name=Blackbeard VOID PRO
Exec=blackbeard_void_pro
Icon=blackbeard-void-pro
StartupWMClass=blackbeard-void-pro
Categories=AudioVideo;Audio;Settings;
Terminal=false
EOF
chmod -R a+rX "$APPDIR"
"$LINUXDEPLOY" --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/blackbeard_void_pro" \
  --desktop-file "$DESKTOP" \
  --icon-file "$ICON" \
  --plugin gtk --output appimage
printf '\nAppImage packaging completed. Test the file outside the repo before release.\n'
