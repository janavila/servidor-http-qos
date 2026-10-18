#!/usr/bin/env bash
# run_experiment.sh - executa N downloads simultaneos de um objeto e registra UMA LINHA CSV
# por cliente (data, commit, cenario, taxa, tempo, throughput, integridade...), para montar a
# tabela de resultados do relatorio sem copiar numeros na mao.
#
# Uso:    CLIENTS=2 SCENARIO=local bash tests/run_experiment.sh <porta> </caminho/do/objeto>
# Ex.:    CLIENTS=5 SCENARIO=ethernet HOST=192.168.0.20 CONFIG_KBPS=2000 \
#         REF_FILE=www/imagem1.jpg bash tests/run_experiment.sh 8080 /imagem1.jpg
#
# Variaveis:
#   HOST=127.0.0.1     endereco do servidor
#   SCENARIO=local     rotulo livre: local | ethernet | wifi
#   CLIENTS=1          downloads simultaneos
#   SRC_IPS=""         IPs de origem separados por espaco, usados em rodizio via `curl --interface`
#                      (ex.: "127.0.0.1 127.0.0.2" para simular IPs diferentes em loopback;
#                       vazio = IP padrao do sistema)
#   CONFIG_KBPS=       taxa configurada no servidor para estes clientes (so informativo, vai no CSV)
#   REF_FILE=          copia local do arquivo para conferir integridade (sha256)
#   REF_SHA=           alternativa ao REF_FILE: hash sha256 esperado (rode `sha256sum` no
#                      servidor e passe aqui, util quando o cliente nao tem o arquivo)
#   OUT=resultados.csv arquivo de saida (cria o cabecalho se nao existir)
#   NOTES=""           observacao livre
set -u
# Numeros com ponto decimal em qualquer idioma do sistema: em pt_BR o awk imprimiria
# "3,00", o que quebraria comparacoes e as colunas do CSV.
export LC_ALL=C

PORT="${1:-8080}"
URLPATH="${2:-/imagem1.jpg}"
HOST="${HOST:-127.0.0.1}"
SCENARIO="${SCENARIO:-local}"
CLIENTS="${CLIENTS:-1}"
SRC_IPS="${SRC_IPS:-}"
CONFIG_KBPS="${CONFIG_KBPS:-}"
REF_FILE="${REF_FILE:-}"
REF_SHA="${REF_SHA:-}"
OUT="${OUT:-resultados.csv}"
NOTES="${NOTES:-}"

for cmd in curl awk sha256sum mktemp; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "ERRO: '$cmd' nao encontrado" >&2; exit 2; }
done
case "$CLIENTS" in ''|*[!0-9]*|0) echo "ERRO: CLIENTS deve ser um inteiro >= 1" >&2; exit 2;; esac

if [ -n "$REF_FILE" ]; then
    [ -r "$REF_FILE" ] || { echo "ERRO: REF_FILE '$REF_FILE' nao legivel" >&2; exit 2; }
    REF_SHA="$(sha256sum "$REF_FILE" | cut -d' ' -f1)"
fi

COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo n/a)"
STAMP="$(date '+%Y-%m-%d %H:%M:%S')"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -s "$OUT" ]; then
    echo "data_hora,commit,cenario,host,porta,url,clientes,cliente,ip_origem,taxa_config_kbps,http,bytes,tempo_s,throughput_kbps,integridade,observacoes" > "$OUT"
fi

# pick_src <indice 1..N>: IP de origem do rodizio de SRC_IPS (vazio = padrao do sistema)
pick_src() {
    [ -z "$SRC_IPS" ] && { echo ""; return; }
    echo "$SRC_IPS" | awk -v i="$1" '{ print $(((i - 1) % NF) + 1) }'
}

echo "Servidor ${HOST}:${PORT}${URLPATH} | cenario=${SCENARIO} | clientes=${CLIENTS} | commit=${COMMIT}"

for i in $(seq 1 "$CLIENTS"); do
    src="$(pick_src "$i")"
    (
        curl -s ${src:+--interface "$src"} -o "$TMP/body.$i" \
             -w '%{http_code} %{size_download} %{time_total}' \
             "http://${HOST}:${PORT}${URLPATH}" > "$TMP/res.$i" 2>"$TMP/err.$i" \
             || echo "0 0 0" > "$TMP/res.$i"
    ) &
done
wait

fail=0
sum_kbps=0
for i in $(seq 1 "$CLIENTS"); do
    src="$(pick_src "$i")"
    read -r code bytes secs < "$TMP/res.$i"
    kbps="$(awk -v b="$bytes" -v t="$secs" 'BEGIN { if (t > 0) printf "%.1f", b * 8 / 1000 / t; else print "0.0" }')"
    integ="n/a"
    if [ -n "$REF_SHA" ]; then
        if [ "$(sha256sum "$TMP/body.$i" | cut -d' ' -f1)" = "$REF_SHA" ]; then integ="ok"; else integ="DIFERENTE"; fail=1; fi
    fi
    [ "$code" = "200" ] || fail=1
    echo "${STAMP},${COMMIT},${SCENARIO},${HOST},${PORT},${URLPATH},${CLIENTS},${i},${src:-padrao},${CONFIG_KBPS},${code},${bytes},${secs},${kbps},${integ},\"${NOTES}\"" >> "$OUT"
    printf "  cliente %-2s origem=%-10s http=%s bytes=%s tempo=%ss  %s kbps  integridade=%s\n" \
        "$i" "${src:-padrao}" "$code" "$bytes" "$secs" "$kbps" "$integ"
    sum_kbps="$(awk -v a="$sum_kbps" -v b="$kbps" 'BEGIN { printf "%.1f", a + b }')"
done

echo "  soma dos throughputs: ${sum_kbps} kbps | linhas gravadas em ${OUT}"
[ "$fail" -eq 0 ] || echo "  ATENCAO: houve resposta diferente de 200 ou arquivo corrompido."
exit "$fail"
