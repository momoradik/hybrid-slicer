#!/usr/bin/env bash
# ci/api_gate.sh — API contract + boundary CI gate.
#
# Starts the core, runs all V-tests, stops the core.
# Exit 0 = all pass. Exit 1 = blocked.
#
# Usage: bash ci/api_gate.sh [port]

set -euo pipefail

PORT="${1:-9000}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
SERVER="$BUILD_DIR/Release/alignmesh_server.exe"
CORE_URL="http://localhost:$PORT"

echo "=== API Contract + Boundary CI Gate ==="
echo "Port: $PORT"
echo "Core: $SERVER"
echo ""

# ── Build ────────────────────────────────────────────────────────────────
echo "Building..."
cmake --build "$BUILD_DIR" --config Release --target alignmesh_server --parallel > /dev/null 2>&1

# ── Start core ───────────────────────────────────────────────────────────
echo "Starting core on port $PORT..."
"$SERVER" "$PORT" &
CORE_PID=$!
sleep 3

# Verify it started.
if ! curl -s --max-time 5 "$CORE_URL/health" > /dev/null 2>&1; then
    echo "ERROR: core did not start"
    kill $CORE_PID 2>/dev/null || true
    exit 1
fi
echo "Core started (PID $CORE_PID)"
echo ""

# ── Run gates ────────────────────────────────────────────────────────────
RESULT=0
node "$PROJECT_DIR/api/ci-gate.js" "$CORE_URL" || RESULT=1

# ── Stop core ────────────────────────────────────────────────────────────
echo ""
echo "Stopping core..."
kill $CORE_PID 2>/dev/null || true
wait $CORE_PID 2>/dev/null || true

exit $RESULT
