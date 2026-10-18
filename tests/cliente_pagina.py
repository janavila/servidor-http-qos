#!/usr/bin/env python3
"""cliente_pagina.py - carrega uma pagina HTML na ORDEM GARANTIDA (HTML primeiro, objetos depois)
e mede tempo e vazao de cada requisicao. Serve para o T08 (RTT/banda) e para a pagina pesada.

Um navegador abre varias conexoes em paralelo e embaralha a ordem; este cliente nao.

Exemplos:
  python3 tests/cliente_pagina.py --porta 8080                         # / e seus objetos, 1 conexao
  python3 tests/cliente_pagina.py --porta 8080 --pagina /pesada.html --conexoes 4
  python3 tests/cliente_pagina.py --porta 8080 --pausa-ms 50           # simula o tempo de o cliente ler o HTML
  python3 tests/cliente_pagina.py --host 192.168.0.20 --origem 192.168.0.31 --porta 8080

Com --conexoes 1 (padrao) tudo usa UMA conexao TCP persistente (keep-alive): o primeiro objeto
e pedido logo depois do HTML, como pede o enunciado. Com N > 1, os objetos sao distribuidos
entre N conexoes, como faria um navegador.
"""
import argparse
import http.client
import queue
import threading
import time
from html.parser import HTMLParser
from urllib.parse import urljoin, urlparse


class Referencias(HTMLParser):
    """Coleta, na ordem do documento, os objetos referenciados (css, js, imagens)."""

    def __init__(self):
        super().__init__()
        self.itens = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "link" and a.get("rel", "").lower() == "stylesheet" and a.get("href"):
            self.itens.append(a["href"])
        elif tag in ("script", "img") and a.get("src"):
            self.itens.append(a["src"])


def conectar(args):
    origem = (args.origem, 0) if args.origem else None
    return http.client.HTTPConnection(args.host, args.porta, timeout=args.timeout, source_address=origem)


def baixar(conn, caminho):
    """GET em uma conexao existente. Retorna (status, bytes, segundos, corpo_se_pedido)."""
    t0 = time.perf_counter()
    conn.request("GET", caminho)
    resp = conn.getresponse()
    corpo = resp.read()
    return resp.status, len(corpo), time.perf_counter() - t0, corpo


def linha(caminho, status, nbytes, seg):
    kbps = nbytes * 8 / 1000 / seg if seg > 0 else 0.0
    return f"  {caminho:<28} http={status}  {nbytes:>10} B  {seg:8.3f} s  {kbps:10.1f} kbps"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--porta", type=int, default=8080)
    p.add_argument("--pagina", default="/")
    p.add_argument("--conexoes", type=int, default=1, help="conexoes TCP para os objetos (padrao 1)")
    p.add_argument("--pausa-ms", type=float, default=0.0, help="espera entre o fim do HTML e o 1o objeto")
    p.add_argument("--origem", help="IP de origem (bind), p.ex. 127.0.0.2 em loopback")
    p.add_argument("--timeout", type=float, default=120.0)
    args = p.parse_args()
    if args.conexoes < 1:
        p.error("--conexoes deve ser >= 1")

    inicio = time.perf_counter()
    principal = conectar(args)
    status, nbytes, seg, corpo = baixar(principal, args.pagina)
    print(f"Servidor {args.host}:{args.porta} | pagina {args.pagina} | conexoes para objetos: {args.conexoes}")
    print(linha(args.pagina, status, nbytes, seg) + "   <- HTML")
    if status != 200:
        raise SystemExit(f"ERRO: a pagina retornou HTTP {status}")

    ref = Referencias()
    ref.feed(corpo.decode("utf-8", "replace"))
    objetos = []
    for r in ref.itens:
        u = urlparse(urljoin(args.pagina, r))
        if not u.netloc and u.path not in objetos:   # so objetos do proprio servidor
            objetos.append(u.path)
    if not objetos:
        print("  (a pagina nao referencia objetos)")
        return

    if args.pausa_ms > 0:
        time.sleep(args.pausa_ms / 1000.0)

    resultados = []          # (ordem, caminho, status, bytes, seg)
    trava = threading.Lock()
    fila = queue.Queue()
    for i, o in enumerate(objetos, 1):
        fila.put((i, o))

    def trabalhador(conn):
        while True:
            try:
                ordem, caminho = fila.get_nowait()
            except queue.Empty:
                break
            st, nb, sg, _ = baixar(conn, caminho)
            with trava:
                resultados.append((ordem, caminho, st, nb, sg))
        conn.close()

    if args.conexoes == 1:
        trabalhador(principal)          # reaproveita a conexao do HTML (keep-alive)
    else:
        principal.close()
        threads = [threading.Thread(target=trabalhador, args=(conectar(args),)) for _ in range(args.conexoes)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

    total_bytes = nbytes
    for ordem, caminho, st, nb, sg in sorted(resultados):
        print(linha(caminho, st, nb, sg) + (f"   <- 1o objeto" if ordem == 1 else ""))
        total_bytes += nb
    total = time.perf_counter() - inicio
    print(f"Total: {total_bytes} B em {total:.3f} s ({total_bytes * 8 / 1000 / total:.1f} kbps medios)")
    if any(r[2] != 200 for r in resultados):
        raise SystemExit("ATENCAO: houve resposta diferente de 200")


if __name__ == "__main__":
    main()
