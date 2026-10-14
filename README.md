# Servidor HTTP/1.1 concorrente com QoS — MVP2

Servidor acadêmico em C para Linux, baseado em sockets POSIX e Pthreads. Serve arquivos estáticos, mantém conexões HTTP/1.1 persistentes e aplica QoS por endereço IPv4.

Integrantes: Jansen Avila, Leonardo Borges e Rodrigo Peraça.

## Dependências

GCC, Make, Pthreads, Bash, curl e Python 3. Para experimentos: wget, tcpdump/Wireshark e IPTraf-ng.

```bash
sudo apt update
sudo apt install build-essential curl wget python3 tcpdump iptraf-ng
```

## Compilação e execução

```bash
make clean
make
./servidor 8080 qos_config.txt 5000
```

CLI: `./servidor <porta> <arquivo_qos> <vazao_maxima_kbps>`.

O arquivo QoS contém um IPv4 e uma taxa por linha:

```text
# IP              taxa em kbps
127.0.0.1         1000
192.168.0.20      1500
```

IPs ausentes recebem 1000 kbps. A soma das taxas de IPs ativos não pode exceder a vazão máxima. Conexões do mesmo IP dividem sua taxa. HTML não recebe pacing; CSS, imagens, JavaScript e demais objetos recebem.

## Arquitetura

- `server`: socket, admissão, threads, integração das métricas e painel.
- `http`: leitura incremental, parser e respostas HTTP.
- `file_handler`: caminhos seguros, MIME e arquivos estáticos.
- `qos_config`: configuração por IP protegida por rwlock.
- `qos_conn_tracker`: conexões, admissão e snapshots protegidos por mutex.
- `qos_throttling`: pacing por prazo absoluto com `CLOCK_MONOTONIC`.
- `qos_metrics`: RTT e banda estimados por IP, protegidos por mutex.

O painel é atualizado a cada dois segundos e exibe somente IPs ativos, conexões, taxas, RTT, banda e vazão global. Snapshots permitem imprimir sem manter mutex durante I/O. A última métrica de cada IP fica em cache para evitar uma corrida entre a desconexão do último socket antigo e a admissão de um novo socket do mesmo IP; IPs sem conexão não aparecem no painel.

## Metodologia das estimativas

T1 é a chegada de uma requisição que retorna HTML. T2 é a chegada do primeiro objeto não HTML subsequente do mesmo IP. O servidor apresenta `T2 - T1` em milissegundos como estimativa de RTT em nível de aplicação. Ela inclui transferência do HTML e processamento do cliente, portanto não equivale ao RTT TCP puro.

A banda estimada usa somente dados observados: `bytes_enviados × 8 / (segundos × 1000)`, em kbps. O tempo cobre o envio do objeto e inclui o pacing configurado. O pareamento por IP permite que o navegador busque o objeto em outro socket; se o mesmo IP iniciar páginas simultâneas, o HTML mais recente substitui a medição pendente.

## Testes

```bash
make test
make test-mvp2
```

Com o servidor já aberto:

```bash
curl -v http://127.0.0.1:8080/
wget http://127.0.0.1:8080/imagem1.jpg
bash tests/test_pacing.sh 8080 1000
PARALLEL=2 bash tests/test_pacing.sh 8080 1000
bash tests/test_keepalive.sh 8080
bash tests/test_concurrent.sh 8080
bash tests/test_metrics.sh 8080 tests/server-test.log
ITERATIONS=10 PARALLEL=5 bash tests/test_stability.sh 8080
```

`make test-mvp2` organiza T05 (QoS por IP), T06 (taxa padrão), T07 (divisão), T08 (RTT/banda), T09 (admissão) e T10 (estabilidade). Tempos e métricas só são considerados resultados após execução real.

Para Valgrind, se já estiver instalado:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./servidor 8080 qos_config.txt 5000
```

## Página pesada

Os JPEGs pequenos permanecem no Git. Para gerar localmente uma página com quatro objetos de aproximadamente 4 MiB adicionais cada:

```bash
SIZE_MB=4 bash tests/create_heavy_test_files.sh
# acessar http://127.0.0.1:8080/heavy/index.html
bash tests/create_heavy_test_files.sh clean
```

## Captura e experimentos

```bash
bash tests/capture_traffic.sh 8080 experimento_local.pcap
INTERFACE=enp0s3 bash tests/capture_traffic.sh 8080 experimento_ethernet.pcap
sudo iptraf-ng
```

O protocolo para Ethernet IEEE 802.3, Wi-Fi IEEE 802.11, IPTraf-ng e a tabela vazia de coleta estão em `docs/experimentos.md`. Não há resultados experimentais fictícios no projeto.

## Limitações

As estimativas são de aplicação, o servidor aceita apenas GET, não implementa TLS e mantém limites fixos de 256 IPs ativos. Os experimentos físicos, capturas e valores finais devem ser coletados pelo grupo no ambiente Linux real.

## Transparência e referências

O desenvolvimento e a revisão receberam apoio de IA generativa (OpenAI Codex). O código produzido deve ser compreendido, revisado e validado pelos integrantes. Quando forem incorporados trechos externos adicionais, sua origem deve ser registrada nos comentários, neste README e no relatório, conforme a especificação da disciplina.
