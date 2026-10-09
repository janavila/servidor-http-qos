#!/usr/bin/env bash
# test_pacing.sh - valida o controle de taxa do servidor (MVP2, Membro 1), fim a fim.
#
# Pre-requisitos:
#   * servidor ja em execucao, com a linha "127.0.0.1 <KBPS>" no arquivo de QoS
#   * executar a partir da raiz do projeto (o script cria arquivos de teste em ./www
#     e os remove ao terminar)
#
# Uso:    bash tests/test_pacing.sh <porta> <kbps_configurado_para_127.0.0.1>
# Ex.:    bash tests/test_pacing.sh 8080 1000
#
# Variaveis opcionais:
#   WWW_DIR=www     diretorio servido pelo servidor
#   TOL=20          tolerancia, em %, sobre o tempo esperado
#   PARALLEL=1      com PARALLEL=2 (ou mais) baixa o mesmo arquivo em N conexoes
#                   simultaneas; so passa quando a contagem de conexoes por IP
#                   (Membro 2) estiver integrada ao servidor.
set -u

PORT="${1:-8080}"
KBPS="${2:-1000}"
WWW_DIR="${WWW_DIR:-www}"
TOL="${TOL:-20}"
PARALLEL="${PARALLEL:-1}"
BASE="http://127.0.0.1:${PORT}"

for cmd in curl awk head mktemp; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "ERRO: '$cmd' nao encontrado" >&2; exit 2; }
done
[ -d "$WWW_DIR" ] || { echo "ERRO: diretorio '$WWW_DIR' nao existe (rode da raiz do projeto ou defina WWW_DIR)" >&2; exit 2; }

# Tamanho que leva ~3 s na taxa configurada (1 kbps = 125 bytes/s).
SIZE=$(( KBPS * 125 * 3 ))
EXPECTED=$(awk -v s="$SIZE" -v k="$KBPS" 'BEGIN { printf "%.2f", s / (k * 125) }')

JPG="$WWW_DIR/_pacing_test.jpg"
HTML="$WWW_DIR/_pacing_test.html"
TMPDIR_PACING="$(mktemp -d)"
cleanup() { rm -f "$JPG" "$HTML"; rm -rf "$TMPDIR_PACING"; }
trap cleanup EXIT

head -c "$SIZE" /dev/zero > "$JPG"
head -c "$SIZE" /dev/zero > "$HTML"

# fetch <caminho> -> imprime "<bytes_baixados> <tempo_total_s>"
fetch() { curl -s -o /dev/null -w '%{size_download} %{time_total}' "$BASE$1"; }

# within <medido> <esperado>: sucesso se estiver na faixa [esp*(1-TOL) - 0,1 ; esp*(1+TOL) + 0,2]
# (a folga absoluta cobre o primeiro bloco, que sai sem espera, e o overhead do curl)
within() {
    awk -v m="$1" -v e="$2" -v t="$TOL" \
        'BEGIN { lo = e * (1 - t / 100) - 0.1; hi = e * (1 + t / 100) + 0.2; exit !(m >= lo && m <= hi) }'
}

fail=0
echo "Taxa configurada: ${KBPS} kbps | arquivo de teste: ${SIZE} bytes | tempo esperado limitado: ~${EXPECTED} s"

# ---- Teste 1: objeto nao-HTML deve respeitar a taxa --------------------------
echo
echo "== Teste 1: /_pacing_test.jpg (limitado)"
read -r got_size got_time <<<"$(fetch /_pacing_test.jpg)"
if [ "${got_size:-0}" != "$SIZE" ]; then
    echo "  FALHA: recebidos ${got_size:-0} bytes, esperado $SIZE (servidor no ar? arquivo servido de '$WWW_DIR'?)"
    fail=1
else
    echo "  tempo medido: ${got_time} s (esperado ~${EXPECTED} s, tolerancia ${TOL}%)"
    if within "$got_time" "$EXPECTED"; then echo "  OK"; else echo "  FALHA: fora da faixa esperada"; fail=1; fi
fi

# ---- Teste 2: HTML do mesmo tamanho nao deve ser limitado --------------------
echo
echo "== Teste 2: /_pacing_test.html (isento)"
read -r got_size got_time <<<"$(fetch /_pacing_test.html)"
if [ "${got_size:-0}" != "$SIZE" ]; then
    echo "  FALHA: recebidos ${got_size:-0} bytes, esperado $SIZE"
    fail=1
else
    echo "  tempo medido: ${got_time} s (limitado levaria ~${EXPECTED} s)"
    if awk -v t="$got_time" 'BEGIN { exit !(t < 1.0) }'; then echo "  OK"; else echo "  FALHA: HTML foi limitado"; fail=1; fi
fi

# ---- Teste 3 (opcional): N conexoes simultaneas dividem a taxa ---------------
if [ "$PARALLEL" -gt 1 ]; then
    echo
    echo "== Teste 3: ${PARALLEL} conexoes simultaneas do mesmo IP (taxa dividida)"
    EXPECTED_N=$(awk -v e="$EXPECTED" -v n="$PARALLEL" 'BEGIN { printf "%.2f", e * n }')
    for i in $(seq 1 "$PARALLEL"); do
        ( fetch /_pacing_test.jpg > "$TMPDIR_PACING/out.$i" ) &
    done
    wait
    for i in $(seq 1 "$PARALLEL"); do
        read -r got_size got_time < "$TMPDIR_PACING/out.$i"
        if [ "${got_size:-0}" != "$SIZE" ]; then
            echo "  conexao $i: FALHA (recebidos ${got_size:-0} bytes)"; fail=1
        elif within "$got_time" "$EXPECTED_N"; then
            echo "  conexao $i: ${got_time} s (esperado ~${EXPECTED_N} s) OK"
        else
            echo "  conexao $i: ${got_time} s (esperado ~${EXPECTED_N} s) FALHA"; fail=1
        fi
    done
fi

echo
if [ "$fail" -eq 0 ]; then echo "Todos os testes de pacing passaram."; else echo "Houve falhas nos testes de pacing."; fi
exit "$fail"
