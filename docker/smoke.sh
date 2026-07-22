#!/usr/bin/env bash
# Headless smoke test: start the compiled OpenKAI with a hardware-free JSON
# config, let it initialize/link/start its modules, then stop it and assert the
# module lifecycle log lines appeared without a crash.
set -uo pipefail

BIN="${OPENKAI_BIN:-./build/OpenKAI}"
CFG="${OPENKAI_CFG:-docker/smoke.json}"
RUN_SECS="${SMOKE_SECONDS:-8}"

if [[ ! -x "$BIN" ]]; then
	echo "SMOKE FAIL: binary not found/executable at $BIN" >&2
	exit 1
fi

echo "=== Running: $BIN $CFG (for ${RUN_SECS}s) ==="
OUT="$(timeout --signal=INT "${RUN_SECS}" "$BIN" "$CFG" 2>&1)"
echo "$OUT"
echo "=== End of output ==="

# Success = the module manager created, initialized and started our module.
if echo "$OUT" | grep -q "Instance created: udp" \
	&& echo "$OUT" | grep -q "Initialized: udp" \
	&& echo "$OUT" | grep -q "Started: udp"; then
	echo "SMOKE PASS: OpenKAI booted and ran the module lifecycle."
	exit 0
fi

echo "SMOKE FAIL: expected module lifecycle log lines not found." >&2
exit 1
