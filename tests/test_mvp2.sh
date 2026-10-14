#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT_MVP2:-18083}"
LOG="tests/server-mvp2.log"
CONFIG="$(mktemp)"
printf '127.0.0.1 1000\n127.0.0.2 1000\n' > "$CONFIG"

./servidor "$PORT" "$CONFIG" 1500 > "$LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -f "$CONFIG"
}
trap cleanup EXIT

for _ in {1..30}; do
    curl -fsS "http://127.0.0.1:${PORT}/" -o /dev/null && break
    sleep 0.1
done

echo "T05 - QoS por IP e HTML isento"
bash tests/test_pacing.sh "$PORT" 1000
echo "T06 - IP nao cadastrado recebe taxa padrao de 1000 kbps"
CLIENT_IP=127.0.0.3 bash tests/test_pacing.sh "$PORT" 1000
echo "T07 - divisao da taxa"
PARALLEL=2 bash tests/test_pacing.sh "$PORT" 1000
echo "T08 - RTT e banda"
bash tests/test_metrics.sh "$PORT" "$LOG"
echo "T09 - controle de admissao"
python3 tests/test_admission.py "$PORT"
echo "T10 - estabilidade"
ITERATIONS=5 PARALLEL=3 bash tests/test_stability.sh "$PORT"

echo "T05-T10 concluidos. Consulte $LOG para os valores realmente medidos."
