#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────
#  UltraHFT – clean launch wrapper for market_data_pipeline_demo
#  Usage: bash tools/run.sh [extra demo args]
#
#  Automatically clears stale DPDK lock/hugepage files before
#  starting the receiver so you never hit the "Cannot create lock"
#  error after a crash or interrupted run.
# ─────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="$REPO_ROOT/build/market_data_pipeline_demo"

if [ ! -x "$BINARY" ]; then
    echo "ERROR: $BINARY not found. Run 'cmake --build build' first." >&2
    exit 1
fi

echo "[run.sh] Clearing stale DPDK runtime files..."
sudo rm -f /dev/hugepages/rtemap_* 2>/dev/null || true
rm -f "/run/user/$(id -u)/dpdk/rte/config" \
      "/run/user/$(id -u)/dpdk/rte/mp_socket" 2>/dev/null || true
echo "[run.sh] Done. Launching demo..."
echo ""

exec "$BINARY" "$@"
