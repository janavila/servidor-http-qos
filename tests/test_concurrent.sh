#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

for clients in 1 2 5 10; do
  pids=()
  for ((i = 1; i <= clients; i++)); do
    curl -fsS "http://127.0.0.1:${PORT}/imagem1.jpg" -o "$TMP_DIR/imagem_${clients}_${i}.jpg" &
    pids+=("$!")
  done
  sleep 5
  
  for pid in "${pids[@]}"; do wait "$pid"; done
  for ((i = 1; i <= clients; i++)); do
    cmp www/imagem1.jpg "$TMP_DIR/imagem_${clients}_${i}.jpg"
  done
  echo "OK: ${clients} cliente(s) simultaneo(s)"
done
