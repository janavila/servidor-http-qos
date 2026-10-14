# Protocolo de experimentos do MVP2

Não registre valores estimados ou fictícios. Preencha a tabela somente durante a execução real.

## Preparação

```bash
make clean && make
SIZE_MB=4 bash tests/create_heavy_test_files.sh
./servidor 8080 qos_config.txt 5000
```

Comandos de clientes, em outro terminal:

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

## Captura e vazão

Loopback:

```bash
bash tests/capture_traffic.sh 8080 experimento_local.pcap
```

Outra interface:

```bash
INTERFACE=enp0s3 bash tests/capture_traffic.sh 8080 experimento_ethernet.pcap
```

No IPTraf-ng, execute `sudo iptraf-ng`, escolha a interface usada e abra o monitor de tráfego IP. Observe a comunicação entre servidor e cliente e registre a taxa durante a região estável da transferência. Não confunda bytes/s com kbps; 1 kbps corresponde a 1000 bits/s.

## Cenário A — Ethernet IEEE 802.3

- Conecte servidor e cliente pela interface Ethernet.
- Registre IPs, taxa do `qos_config.txt`, tamanho dos objetos e número de clientes.
- Inicie tcpdump/Wireshark e IPTraf-ng na interface Ethernet.
- Execute exatamente a mesma sequência de downloads usada no Wi-Fi.
- Registre somente valores observados.

## Cenário B — Wi-Fi IEEE 802.11

- Conecte servidor e cliente pela interface Wi-Fi.
- Mantenha taxa, arquivos, número de clientes e comandos do cenário Ethernet.
- Capture na interface Wi-Fi e registre as mesmas métricas.
- Anote sinal, distância e interferências relevantes.

## Planilha de coleta

| Cenário | Rede | Clientes | IP | Taxa configurada (kbps) | Conexões | Taxa/conn (kbps) | Arquivo/tamanho | Tempo (s) | Throughput (kbps) | RTT estimado (ms) | Banda estimada (kbps) | Admissão | Observações |
|---|---|---:|---|---:|---:|---:|---|---:|---:|---:|---:|---|---|
| [INSERIR] | [INSERIR] | | | | | | | | | | | | |

Ao terminar: `bash tests/create_heavy_test_files.sh clean`.
