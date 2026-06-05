#!/usr/bin/env bash
# install.sh — Download and install Merak on Linux
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

INSTALL_DIR="${MERAK_HOME:-$HOME/merak}"
REPO="ULookup/Merak"

info "Merak Installer"
info "Install to: $INSTALL_DIR"

# 1. Get latest release
info "Fetching latest release..."
LATEST_TAG=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
if [ -z "$LATEST_TAG" ]; then
  error "Cannot fetch latest release. Check internet connection."
fi
info "Latest version: $LATEST_TAG"

# 2. Download
URL="https://github.com/$REPO/releases/download/$LATEST_TAG/merak-linux-x64.tar.gz"
info "Downloading $URL ..."
curl -fsSL "$URL" -o /tmp/merak.tar.gz || error "Download failed"

# 3. Extract
mkdir -p "$INSTALL_DIR"
tar xzf /tmp/merak.tar.gz -C "$INSTALL_DIR"
rm /tmp/merak.tar.gz
info "Extracted to $INSTALL_DIR"

# 4. Make executable
chmod +x "$INSTALL_DIR/merak"
chmod +x "$INSTALL_DIR/start.sh" 2>/dev/null || true

# 5. Create desktop shortcut
DESKTOP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_DIR/merak.desktop" << DESKTOPEOF
[Desktop Entry]
Name=Merak
Comment=AI Agent for Novel Writing
Exec=$INSTALL_DIR/start.sh
Icon=$INSTALL_DIR/icon.png
Terminal=true
Type=Application
Categories=Utility;
DESKTOPEOF
info "Desktop shortcut created"

# 6. Init config if not exists
if [ ! -f "$HOME/.merak/settings.local.json" ]; then
  "$INSTALL_DIR/merak" --init 2>/dev/null || true
  info "Config template created at ~/.merak/settings.local.json"
  warn "Edit ~/.merak/settings.local.json to add your API key"
fi

info "Done! Run: $INSTALL_DIR/start.sh"
