#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# ─── Config ───
PG_CONTAINER="merak-pg"
PG_PORT="${PG_PORT:-5432}"
PG_USER="${PG_USER:-merak}"
PG_PASS="${PG_PASS:-merak}"
PG_DB="${PG_DB:-merak}"
PG_CONNINFO="host=127.0.0.1 port=${PG_PORT} dbname=${PG_DB} user=${PG_USER} password=${PG_PASS}"
SERVER_PORT="${SERVER_PORT:-3888}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()   { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
err()   { echo -e "${RED}[x]${NC} $*"; }

# ─── Step 1: Check Docker ───
log "Checking Docker..."
if ! command -v docker &>/dev/null; then
    err "Docker not found. Please install Docker first."
    exit 1
fi
if ! docker info &>/dev/null; then
    err "Docker daemon not running or permission denied."
    exit 1
fi

# ─── Step 2: Start PostgreSQL ───
if docker ps --format '{{.Names}}' | grep -q "^${PG_CONTAINER}$"; then
    log "PostgreSQL container '${PG_CONTAINER}' already running."
else
    if docker ps -a --format '{{.Names}}' | grep -q "^${PG_CONTAINER}$"; then
        log "Starting existing PostgreSQL container..."
        docker start "${PG_CONTAINER}" >/dev/null
    else
        log "Creating PostgreSQL container '${PG_CONTAINER}'..."
        docker run -d \
            --name "${PG_CONTAINER}" \
            -e "POSTGRES_USER=${PG_USER}" \
            -e "POSTGRES_PASSWORD=${PG_PASS}" \
            -e "POSTGRES_DB=${PG_DB}" \
            -p "${PG_PORT}:5432" \
            postgres:16-alpine >/dev/null
    fi
fi

# ─── Step 3: Wait for PostgreSQL ───
log "Waiting for PostgreSQL to be ready..."
for i in $(seq 30); do
    if docker exec "${PG_CONTAINER}" pg_isready -U "${PG_USER}" &>/dev/null; then
        break
    fi
    sleep 1
done
if ! docker exec "${PG_CONTAINER}" pg_isready -U "${PG_USER}" &>/dev/null; then
    err "PostgreSQL failed to start after 30s."
    exit 1
fi
log "PostgreSQL is ready."

# ─── Step 4: Apply schema ───
SCHEMA_FILE="${PROJECT_DIR}/libs/worldbuilding/schema.sql"
if [ -f "$SCHEMA_FILE" ]; then
    log "Applying schema..."
    docker exec -i "${PG_CONTAINER}" psql -U "${PG_USER}" -d "${PG_DB}" < "$SCHEMA_FILE" 2>&1 | \
        grep -v "NOTICE:" | grep -v "already exists" || true
    log "Schema applied."
fi

# ─── Step 5: Write config ───
CONFIG_DIR="${HOME}/.merak"
CONFIG_FILE="${CONFIG_DIR}/settings.local.json"
mkdir -p "${CONFIG_DIR}"

if [ -f "$CONFIG_FILE" ]; then
    log "Config file already exists at ${CONFIG_FILE}, updating db_connection..."
    # Use jq if available, otherwise warn
    if command -v jq &>/dev/null; then
        tmp=$(mktemp)
        jq --arg db "${PG_CONNINFO}" '.memory.db_connection = $db' "$CONFIG_FILE" > "$tmp"
        mv "$tmp" "$CONFIG_FILE"
    else
        warn "jq not found — cannot update existing config. Please add manually:"
        warn "  \"memory\": { \"db_connection\": \"${PG_CONNINFO}\" }"
    fi
else
    log "Creating config file..."
    cat > "$CONFIG_FILE" <<EOFCONFIG
{
  "llm": {
    "provider": "openai",
    "api_key": "sk-your-api-key-here",
    "default_model": "gpt-4o",
    "max_output_tokens": 4096
  },
  "agent": {
    "system_prompt": "You are a helpful AI assistant. Use tools to complete tasks.",
    "max_tool_turns": 25,
    "permission_mode": "default"
  },
  "memory": {
    "enabled": true,
    "db_connection": "${PG_CONNINFO}"
  }
}
EOFCONFIG
fi

log "Config: ${PG_CONNINFO}"

# ─── Step 6: Build ───
log "Building project..."
cd "$PROJECT_DIR"
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target merak-cli -j"$(nproc)"

# ─── Step 7: Start server ───
log "Starting merak serve on port ${SERVER_PORT}..."
echo ""
echo "  ┌─────────────────────────────────────────┐"
echo "  │   PostgreSQL : docker (${PG_CONTAINER})           │"
echo "  │   Server     : http://127.0.0.1:${SERVER_PORT}    │"
echo "  │   TUI        : merak tui                  │"
echo "  └─────────────────────────────────────────┘"
echo ""

exec ./build/cli/merak serve --port "${SERVER_PORT}"
