#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
LOG="${2:-tests/server-test.log}"

python3 tests/test_keepalive.py "$PORT"
for _ in {1..30}; do
    grep -q "Metricas 127.0.0.1: RTT estimado" "$LOG" 2>/dev/null && {
        echo "T08 OK: servidor produziu RTT e banda estimados a partir de dados medidos."
        exit 0
    }
    sleep 0.1
done
echo "T08 FALHA: metricas nao apareceram em $LOG" >&2
exit 1
