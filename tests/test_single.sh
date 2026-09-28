#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-8080}"
BASE_URL="http://127.0.0.1:${PORT}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

status="$(curl -sS -o "$TMP_DIR/index.html" -w '%{http_code}' "$BASE_URL/")"
test "$status" = "200"
grep -q "Servidor HTTP/1.1" "$TMP_DIR/index.html"

status="$(curl -sS -o /dev/null -w '%{http_code}' "$BASE_URL/arquivo_que_nao_existe.html")"
test "$status" = "404"

status="$(curl -sS -X POST -o /dev/null -w '%{http_code}' "$BASE_URL/")"
test "$status" = "405"

status="$(curl --path-as-is -sS -o /dev/null -w '%{http_code}' "$BASE_URL/../../etc/passwd")"
test "$status" = "403"

curl -sS "$BASE_URL/imagem1.jpg" -o "$TMP_DIR/imagem1.jpg"
cmp www/imagem1.jpg "$TMP_DIR/imagem1.jpg"
echo "OK: respostas 200, 403, 404 e 405; HTML e imagem integros"
