#!/usr/bin/env bash
# comparar_eth_wifi.sh - roda, NO CLIENTE (notebook), a comparacao Ethernet x Wi-Fi alternando os
# cenarios a cada repeticao (E, W, E, W, ...) e gravando tudo em CSV via run_experiment.sh.
#
# Antes de cada cenario o script PARA e pede que voce ative so aquela interface (faca isso em OUTRO
# terminal) e tecle ENTER. Depois confere, pela tabela de rotas, se o trafego para o servidor realmente
# sai pela interface esperada; se nao sair, avisa e pede de novo. Isso evita rotular como "wifi" uma
# medida que passou pelo cabo.
#
# Uso:
#   HOST=192.168.8.8 REF_SHA=<hash sha256 do arquivo> CONFIG_KBPS=4000 \
#     bash tests/comparar_eth_wifi.sh [porta=8080] [objeto=/heavy1.jpg] [repeticoes=3]
#
# Variaveis:
#   HOST          (obrigatoria) IP do servidor
#   REF_SHA       (obrigatoria) hash do objeto: `sha256sum www/heavy1.jpg` NA MAQUINA DO SERVIDOR.
#                 Cole o valor de 64 caracteres; NAO deixe os sinais < >.
#   CONFIG_KBPS   taxa configurada no servidor para o notebook (so informativo, vai no CSV)
#   ETH_DEV       interface cabeada do cliente   (padrao enp7s0)
#   WIFI_DEV      interface Wi-Fi do cliente     (padrao wlp6s0)
#   CLIENT_LIST   quantidades de conexoes simultaneas a medir (padrao "1 2")
#   OUT           CSV de saida (padrao evidencias/resultados.csv)
#   NO_PROMPT=1   nao pede ENTER (so para testar o script; aborta se a interface nao bater)
set -u
export LC_ALL=C

PORT="${1:-8080}"
OBJ="${2:-/heavy1.jpg}"
REPS="${3:-3}"
HOST="${HOST:-}"
REF_SHA="${REF_SHA:-}"
CONFIG_KBPS="${CONFIG_KBPS:-}"
ETH_DEV="${ETH_DEV:-enp7s0}"
WIFI_DEV="${WIFI_DEV:-wlp6s0}"
CLIENT_LIST="${CLIENT_LIST:-1 2}"
OUT="${OUT:-evidencias/resultados.csv}"
NO_PROMPT="${NO_PROMPT:-0}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

[ -n "$HOST" ] || { echo "ERRO: defina HOST (IP do servidor)" >&2; exit 2; }
case "$REPS" in ''|*[!0-9]*|0) echo "ERRO: repeticoes deve ser inteiro >= 1" >&2; exit 2;; esac
if ! printf '%s' "$REF_SHA" | grep -Eq '^[0-9a-fA-F]{64}$'; then
    echo "ERRO: REF_SHA deve ser o hash sha256 de 64 caracteres hexadecimais (sem < >)." >&2
    echo "      Na maquina do servidor:  sha256sum www/heavy1.jpg" >&2
    exit 2
fi
for cmd in curl ip awk; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "ERRO: '$cmd' nao encontrado" >&2; exit 2; }
done
mkdir -p "$(dirname "$OUT")"
LOGDIR="$(dirname "$OUT")"

# rota_para_servidor -> imprime "<interface> <ip_de_origem>"
rota() {
    ip route get "$HOST" 2>/dev/null | awk '
        { for (i = 1; i <= NF; i++) { if ($i == "dev") d = $(i + 1); if ($i == "src") s = $(i + 1) } }
        END { print d, s }'
}

echo "Servidor ${HOST}:${PORT}${OBJ} | repeticoes=${REPS} | conexoes=${CLIENT_LIST} | saida=${OUT}"
echo "Ordem: (ethernet, wifi) x ${REPS}"

for rep in $(seq 1 "$REPS"); do
    for cenario in ethernet wifi; do
        if [ "$cenario" = "ethernet" ]; then want="$ETH_DEV"; else want="$WIFI_DEV"; fi

        while :; do
            if [ "$NO_PROMPT" != "1" ]; then
                echo
                echo ">>> repeticao ${rep}/${REPS}: ative SOMENTE o cenario '${cenario}' (interface ${want}) em outro terminal e tecle ENTER"
                read -r _
            fi
            read -r dev src <<<"$(rota)"
            if [ "$dev" = "$want" ]; then break; fi
            echo "    ATENCAO: o trafego para ${HOST} sai por '${dev:-?}' e nao por '${want}'. Ajuste as interfaces e tente de novo."
            [ "$NO_PROMPT" = "1" ] && { echo "    (NO_PROMPT=1: abortando)"; exit 3; }
        done

        echo "--- ${cenario} | rep ${rep} | interface ${dev} | IP de origem ${src}"
        if command -v ping >/dev/null 2>&1; then
            { echo "# ${cenario} rep${rep} ip_origem=${src}"; ping -c 10 -q "$HOST" | tail -2; } | tee -a "${LOGDIR}/ping_${cenario}.txt"
        fi

        for n in $CLIENT_LIST; do
            CLIENTS="$n" SCENARIO="$cenario" HOST="$HOST" CONFIG_KBPS="$CONFIG_KBPS" \
            NOTES="rep${rep} ip_origem=${src}" REF_SHA="$REF_SHA" OUT="$OUT" \
                bash "${HERE}/run_experiment.sh" "$PORT" "$OBJ"
        done
    done
done

echo
echo "Concluido. Linhas gravadas em ${OUT}"
echo "Media e desvio por cenario e numero de conexoes:"
awk -F, 'NR > 1 { k = $3 " clientes=" $7; n[k]++; s[k] += $14; q[k] += $14 * $14 }
END { for (k in n) { m = s[k] / n[k]; v = (n[k] > 1) ? (q[k] - n[k] * m * m) / (n[k] - 1) : 0;
      printf "  %s: n=%d media=%.1f kbps dp=%.1f\n", k, n[k], m, sqrt(v > 0 ? v : 0) } }' "$OUT" | sort
