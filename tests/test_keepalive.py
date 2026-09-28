#!/usr/bin/env python3
"""Cliente minimo: faz duas requisicoes HTTP na mesma conexao TCP."""
import socket
import sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

def receive_response(connection):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError("conexao encerrada antes dos cabecalhos")
        data += chunk
    headers, body = data.split(b"\r\n\r\n", 1)
    values = {}
    for line in headers.split(b"\r\n")[1:]:
        name, value = line.split(b":", 1)
        values[name.lower()] = value.strip()
    expected = int(values[b"content-length"])
    while len(body) < expected:
        chunk = connection.recv(min(65536, expected - len(body)))
        if not chunk:
            raise RuntimeError("conexao encerrada antes do corpo")
        body += chunk
    return headers, body

with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
    connection.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    first_headers, first_body = receive_response(connection)
    assert first_headers.startswith(b"HTTP/1.1 200 OK")
    assert b"Connection: keep-alive" in first_headers
    assert b"Servidor HTTP/1.1" in first_body

    connection.sendall(b"GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    second_headers, second_body = receive_response(connection)
    assert second_headers.startswith(b"HTTP/1.1 200 OK")
    assert b"Connection: close" in second_headers
    assert b".galeria" in second_body

print("OK: duas respostas recebidas na mesma conexao TCP")
