#!/usr/bin/env bash
set -euo pipefail

ACTION="${1:-create}"
SIZE_MB="${SIZE_MB:-4}"
DEST="www/heavy"

if [ "$ACTION" = "clean" ]; then
    rm -rf "$DEST"
    echo "Arquivos pesados removidos de $DEST"
    exit 0
fi

case "$SIZE_MB" in
    ''|*[!0-9]*) echo "SIZE_MB deve ser inteiro positivo" >&2; exit 2 ;;
esac
[ "$SIZE_MB" -gt 0 ] || { echo "SIZE_MB deve ser maior que zero" >&2; exit 2; }

mkdir -p "$DEST"
for i in 1 2 3 4; do
    cp "www/imagem${i}.jpg" "$DEST/imagem${i}.jpg"
    dd if=/dev/zero bs=1M count="$SIZE_MB" >> "$DEST/imagem${i}.jpg" 2>/dev/null
done
cp www/style.css "$DEST/style.css"
cat > "$DEST/index.html" <<'HTML'
<!doctype html><html lang="pt-BR"><meta charset="utf-8">
<title>Pagina pesada QoS</title><link rel="stylesheet" href="/heavy/style.css">
<h1>Pagina pesada para experimentos</h1>
<img src="/heavy/imagem1.jpg" alt="Teste 1"><img src="/heavy/imagem2.jpg" alt="Teste 2">
<img src="/heavy/imagem3.jpg" alt="Teste 3"><img src="/heavy/imagem4.jpg" alt="Teste 4">
HTML

echo "Pagina gerada em /heavy/index.html"
du -h "$DEST"/*
echo "Para limpar: bash tests/create_heavy_test_files.sh clean"
