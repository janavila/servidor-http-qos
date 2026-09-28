#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-18080}"
./servidor "$PORT" > tests/server-test.log 2>&1 &
SERVER_PID="$!"
trap 'kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true' EXIT

for _ in {1..30}; do
  curl -fsS "http://127.0.0.1:${PORT}/" -o /dev/null && break
  sleep 0.1
done

bash tests/test_single.sh "$PORT"
bash tests/test_keepalive.sh "$PORT"
bash tests/test_concurrent.sh "$PORT"
echo "Todos os testes passaram."
