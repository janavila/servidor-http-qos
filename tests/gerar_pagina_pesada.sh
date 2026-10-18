#!/usr/bin/env bash
# gerar_pagina_pesada.sh - cria a "pagina pesada" exigida pela avaliacao: um HTML que referencia
# varias imagens de VARIOS MEGABYTES.
#
# Como funciona: copia www/imagem1..4.jpg para www/heavy1..4.jpg e preenche o fim de cada copia com
# zeros ate o tamanho pedido. A imagem continua valida (os decodificadores ignoram o que vem
# depois do fim do JPEG), entao o navegador ainda exibe a figura, mas o arquivo tem N MB reais.
# Tambem cria www/pesada.html (referencia /style.css e as 4 imagens).
#
# Uso:    bash tests/gerar_pagina_pesada.sh [MB_por_imagem=3]
# Ex.:    bash tests/gerar_pagina_pesada.sh 4        # 4 imagens de 4 MB
#
# Variaveis: WWW_DIR=www  |  RATE_KBPS=4000 (so para imprimir o tempo estimado por imagem)
#
# NAO versione os arquivos gerados: acrescente ao .gitignore
#     www/heavy*.jpg
#     www/pesada.html
# Para remover: rm -f www/heavy*.jpg www/pesada.html
set -eu
export LC_ALL=C

MB="${1:-3}"
WWW="${WWW_DIR:-www}"
RATE="${RATE_KBPS:-4000}"

case "$MB" in ''|*[!0-9]*|0) echo "ERRO: informe um inteiro >= 1 (MB por imagem)" >&2; exit 2;; esac
[ -d "$WWW" ] || { echo "ERRO: diretorio '$WWW' nao existe (rode da raiz do projeto ou defina WWW_DIR)" >&2; exit 2; }
for cmd in stat truncate cp; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "ERRO: '$cmd' nao encontrado" >&2; exit 2; }
done

TARGET=$(( MB * 1000000 ))   # MB = 10^6 bytes, para bater com a conta de kbps (1 kbps = 125 B/s)

for i in 1 2 3 4; do
    src="$WWW/imagem$i.jpg"
    dst="$WWW/heavy$i.jpg"
    [ -f "$src" ] || { echo "ERRO: '$src' nao existe (a pagina de testes do MVP1 deve ter imagem1..4.jpg)" >&2; exit 2; }
    cp "$src" "$dst"
    size=$(stat -c %s "$dst")
    if [ "$size" -lt "$TARGET" ]; then
        truncate -s "$TARGET" "$dst"
    else
        echo "aviso: $src ja tem $size bytes (>= alvo); copia mantida sem preenchimento"
    fi
done

cat > "$WWW/pesada.html" <<'EOF'
<!doctype html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Servidor HTTP/1.1 - pagina pesada</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <main>
    <h1>Pagina pesada (teste de QoS)</h1>
    <p>Quatro imagens de varios megabytes, servidas com limitacao de taxa por IP.</p>
    <section class="galeria">
      <figure><img src="/heavy1.jpg" alt="Imagem pesada 1"><figcaption>Imagem 1</figcaption></figure>
      <figure><img src="/heavy2.jpg" alt="Imagem pesada 2"><figcaption>Imagem 2</figcaption></figure>
      <figure><img src="/heavy3.jpg" alt="Imagem pesada 3"><figcaption>Imagem 3</figcaption></figure>
      <figure><img src="/heavy4.jpg" alt="Imagem pesada 4"><figcaption>Imagem 4</figcaption></figure>
    </section>
  </main>
</body>
</html>
EOF

echo "Pagina pesada criada em '$WWW':"
ls -l "$WWW"/heavy*.jpg "$WWW/pesada.html" | awk '{ printf "  %-22s %12d bytes\n", $9, $5 }'
awk -v b="$TARGET" -v r="$RATE" 'BEGIN {
    printf "Tempo estimado por imagem a %d kbps (1 conexao): %.1f s | as 4 imagens: %.1f s\n", r, b * 8 / (r * 1000), 4 * b * 8 / (r * 1000)
}'
echo "Lembrete: o IP do cliente precisa estar no qos_config.txt, senao vale a taxa padrao de 1000 kbps."
