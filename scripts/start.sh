#!/usr/bin/env bash
# start.sh — Launch Merak from a green package (Linux)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Merak Quick Start ==="

# 1. Start merak serve in background
echo "[1/3] Starting merak serve..."
./merak serve --port 3888 &
SERVER_PID=$!
echo "  merak serve PID: $SERVER_PID"

# 2. Wait for server to be ready
echo "[2/3] Waiting for server..."
for i in $(seq 1 30); do
  if curl -s http://127.0.0.1:3888/v1/runtime > /dev/null 2>&1; then
    echo "  Server ready."
    break
  fi
  sleep 0.5
done

# 3. Open TUI in a new terminal
echo "[3/3] Launching TUI..."
if command -v gnome-terminal &>/dev/null; then
  gnome-terminal -- bash -c "./merak tui; echo 'Press Enter to close...'; read"
elif command -v konsole &>/dev/null; then
  konsole -e bash -c "./merak tui; echo 'Press Enter to close...'; read"
elif command -v xterm &>/dev/null; then
  xterm -e "./merak tui" &
elif command -v x-terminal-emulator &>/dev/null; then
  x-terminal-emulator -e "./merak tui" &
else
  echo "  No terminal found. Start TUI manually: ./merak tui"
fi

echo "=== Merak is running ==="
echo "  Server: http://127.0.0.1:3888"
echo "  Press Ctrl+C to stop server"
echo ""

# Wait for server
trap "kill $SERVER_PID 2>/dev/null; exit 0" INT TERM
wait $SERVER_PID
