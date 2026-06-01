#!/usr/bin/env bash
set -euo pipefail

# Merak Agent — install script
# Usage: ./scripts/install.sh [--prefix ~/.local]

PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"
CONFIG_DIR="$HOME/.merak"

echo "=== Merak Agent Install ==="
echo "  binary  → $BIN_DIR/merak"
echo "  config  → $CONFIG_DIR/"
echo ""

# 1. Build (if not already built)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

if [ ! -f "$BUILD_DIR/cli/merak-cli" ]; then
    echo "[1/3] Building..."
    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
        -DCMAKE_TOOLCHAIN_FILE=Debug/generators/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Debug
    cmake --build "$BUILD_DIR"
else
    echo "[1/3] Already built"
fi

# 2. Install binary
echo "[2/3] Installing binary..."
mkdir -p "$BIN_DIR"
cp "$BUILD_DIR/cli/merak-cli" "$BIN_DIR/merak"

# 3. Install example config (if not exists)
echo "[3/3] Installing config..."
mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/settings.json" ]; then
    if [ -f "$PROJECT_DIR/.merak/settings.json" ]; then
        cp "$PROJECT_DIR/.merak/settings.json" "$CONFIG_DIR/settings.json"
    elif [ -f "$PROJECT_DIR/config/settings.json.example" ]; then
        cp "$PROJECT_DIR/config/settings.json.example" "$CONFIG_DIR/settings.json"
    else
        cat > "$CONFIG_DIR/settings.json" << 'JSONEOF'
{
    "llm": {
        "provider": "openai",
        "api_base_url": "https://api.openai.com/v1",
        "default_model": "gpt-4o"
    },
    "agent": {
        "system_prompt": "You are a helpful AI assistant. Use tools to complete tasks."
    }
}
JSONEOF
    fi
fi

echo ""
echo "Done. Now run: merak --init"
echo ""

# PATH reminder
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo "Add $BIN_DIR to your PATH:"
    echo "  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc"
    echo "  source ~/.bashrc"
    echo ""
fi
