#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
descritores=()

# Envia cabeçalhos incompletos para manter as threads aguardando dados.
for i in $(seq 1 10); do
    exec {socket_fd}<>"/dev/tcp/127.0.0.1/${PORT}"

    printf 'GET / HTTP/1.1\r\nHost: localhost:%s\r\n' \
        "$PORT" >&"$socket_fd"

    descritores+=("$socket_fd")
done

echo "10 conexões abertas."

pid="$(pgrep -o -x servidor)"

ps -L -p "$pid" -o pid,lwp,nlwp,comm

echo "As conexões permanecerão abertas por 30 segundos."
sleep 30

for socket_fd in "${descritores[@]}"; do
    exec {socket_fd}>&-
done
