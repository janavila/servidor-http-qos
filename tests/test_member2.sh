#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT_MEMBER2:-18082}"
LOG="tests/server-member2.log"

expect_invalid() {
    if ./servidor "$@" > /dev/null 2>&1; then
        echo "FALHA: CLI invalida foi aceita: $*"
        exit 1
    fi
}

expect_invalid
expect_invalid 0 qos_config.txt 5000
expect_invalid 8080 qos_config.txt 0
expect_invalid 8080 qos_config.txt texto

./servidor "$PORT" qos_config.txt 5000 > "$LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT

for _ in {1..30}; do
    if python3 - "$PORT" <<'PY' >/dev/null 2>&1
import socket
import sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=0.2)
s.close()
PY
    then
        break
    fi
    sleep 0.1
done

# Dois GETs no mesmo socket validam que keep-alive nao cria um segundo registro.
python3 - "$PORT" <<'PY'
import socket
import sys

def response(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        data += sock.recv(4096)
    head, body = data.split(b"\r\n\r\n", 1)
    length = 0
    for line in head.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    while len(body) < length:
        body += sock.recv(4096)

s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=2)
s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n")
response(s)
s.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
response(s)
s.close()
PY

sleep 0.1
kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null || true
trap - EXIT

admitted=$(grep -c "Cliente admitido: 127.0.0.1" "$LOG" || true)
removed=$(grep -c "Cliente removido: 127.0.0.1" "$LOG" || true)
gets=$(grep -c "GET /" "$LOG" || true)

# A sondagem inicial tambem abre uma conexao; cada socket deve ter exatamente
# uma admissao e uma remocao, enquanto o socket keep-alive executa dois GETs.
if [ "$admitted" -ne "$removed" ] || [ "$gets" -lt 2 ]; then
    echo "FALHA: integracao keep-alive/admissao inconsistente"
    cat "$LOG"
    exit 1
fi

echo "CLI e integracao keep-alive do Membro 2 passaram."
