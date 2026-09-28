#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
OUTPUT="${2:-teste_http.pcap}"

echo "Iniciando captura em $OUTPUT. Pressione Ctrl+C quando terminar os testes."
echo "Em outro terminal, execute: curl -v http://localhost:${PORT}/"
sudo tcpdump -i lo -w "$OUTPUT" "port $PORT"
