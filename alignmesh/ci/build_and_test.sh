#!/usr/bin/env bash
# ci/build_and_test.sh
#
# Build, run the full test suite, then enforce same-machine numeric
# reproducibility by running repro_manifest twice and asserting
# byte-identical output.
#
# Usage:
#   bash ci/build_and_test.sh [Release|Debug]   (default: Release)

set -euo pipefail

BUILD_TYPE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# ---- configure --------------------------------------------------------------
echo "=== Configure ($BUILD_TYPE) ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# ---- build ------------------------------------------------------------------
echo "=== Build ==="
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

# ---- test suite -------------------------------------------------------------
echo "=== Test suite (ctest) ==="
ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure

# ---- reproducibility gate ---------------------------------------------------
echo "=== Reproducibility gate ==="

# Locate repro_manifest binary (path varies by generator / OS).
REPRO_BIN=""
for candidate in \
    "$BUILD_DIR/tests/repro_manifest" \
    "$BUILD_DIR/tests/repro_manifest.exe" \
    "$BUILD_DIR/tests/$BUILD_TYPE/repro_manifest.exe" \
    "$BUILD_DIR/tests/$BUILD_TYPE/repro_manifest"; do
    if [ -f "$candidate" ]; then
        REPRO_BIN="$candidate"
        break
    fi
done

if [ -z "$REPRO_BIN" ]; then
    echo "ERROR: repro_manifest binary not found in $BUILD_DIR/tests/"
    exit 1
fi

echo "Binary: $REPRO_BIN"

RUN1="$BUILD_DIR/repro_run1.txt"
RUN2="$BUILD_DIR/repro_run2.txt"

echo "  Run 1..."
"$REPRO_BIN" > "$RUN1"

echo "  Run 2..."
"$REPRO_BIN" > "$RUN2"

# Primary check: byte-level diff.
if ! diff -q "$RUN1" "$RUN2" > /dev/null 2>&1; then
    echo "FAIL: numeric outputs differ between runs!"
    echo ""
    diff "$RUN1" "$RUN2" || true
    exit 1
fi

# Compute repro_hash for the audit trail.
compute_hash() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v certutil > /dev/null 2>&1; then
        # Windows fallback (certutil outputs multi-line)
        certutil -hashfile "$1" SHA256 2>/dev/null | sed -n '2p' | tr -d ' '
    else
        echo "(no hash tool available)"
    fi
}

HASH=$(compute_hash "$RUN1")

echo ""
echo "PASS: same-machine reproducibility confirmed."
echo "repro_hash: $HASH"
