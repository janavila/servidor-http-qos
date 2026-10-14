#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
OUTPUT="${2:-experimento_local.pcap}"
INTERFACE="${INTERFACE:-lo}"

echo "Iniciando captura em $OUTPUT na interface $INTERFACE."
echo "Em outro terminal: curl -v http://127.0.0.1:${PORT}/ http://127.0.0.1:${PORT}/imagem1.jpg"
echo "Pressione Ctrl+C quando terminar."
sudo tcpdump -i "$INTERFACE" -w "$OUTPUT" "port $PORT"
