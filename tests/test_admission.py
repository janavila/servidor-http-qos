#!/usr/bin/env python3
"""T09: dois IPs loopback cuja soma excede a capacidade configurada."""
import socket
import sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
first = socket.socket()
second = socket.socket()
try:
    first.bind(("127.0.0.1", 0))
    first.settimeout(2)
    first.connect(("127.0.0.1", port))

    second.bind(("127.0.0.2", 0))
    second.settimeout(2)
    second.connect(("127.0.0.1", port))
    second.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    try:
        received = second.recv(1)
    except ConnectionResetError:
        received = b""
    if received != b"":
        raise AssertionError("segunda conexao recebeu resposta, mas deveria ser rejeitada")
finally:
    first.close()
    second.close()
print("T09 OK: conexao que excederia a vazao maxima foi rejeitada.")
