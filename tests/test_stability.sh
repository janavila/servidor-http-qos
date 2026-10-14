#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
ITERATIONS="${ITERATIONS:-10}"
PARALLEL="${PARALLEL:-5}"
BASE="http://127.0.0.1:${PORT}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

for ((round = 1; round <= ITERATIONS; round++)); do
    pids=()
    for ((client = 1; client <= PARALLEL; client++)); do
        curl -fsS "$BASE/imagem1.jpg" -o "$TMP_DIR/${round}-${client}.jpg" &
        pids+=("$!")
    done
    for pid in "${pids[@]}"; do wait "$pid"; done
done

curl -fsS "$BASE/" -o /dev/null
echo "T10 OK: servidor respondeu apos ${ITERATIONS} rodadas de ${PARALLEL} clientes."
