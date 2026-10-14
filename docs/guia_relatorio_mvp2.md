# Guia para o relatório SBC — MVP2

Este arquivo é uma base de escrita. Não contém resultados inventados.

## Resumo

Apresentar objetivo, servidor HTTP/1.1 concorrente, QoS por IP, metodologia e principais resultados reais. `[INSERIR RESULTADO REAL]`

## Introdução

Contextualizar HTTP, compartilhamento de capacidade e objetivo da avaliação em Ethernet e Wi-Fi.

## Desenvolvimento

Arquitetura: `server` aceita sockets e cria threads; `http` interpreta mensagens; `file_handler` entrega arquivos; `qos_config` carrega taxas; `qos_conn_tracker` faz admissão, contagem e snapshot; `qos_throttling` executa pacing; `qos_metrics` estima RTT e banda; uma thread imprime o painel.

- QoS por IP: taxa configurada ou 1000 kbps por padrão.
- Pacing: prazos absolutos com `CLOCK_MONOTONIC`; HTML é isento.
- Divisão: taxa do IP dividida pelas conexões TCP ativas.
- Concorrência: mutex/rwlock e snapshots curtos; locks não permanecem durante I/O.
- Admissão: um novo IP é aceito somente quando a soma das taxas alocadas não supera o limite global.
- RTT estimado: `T2 - T1`, onde T1 é a chegada do HTML e T2 a chegada do primeiro objeto não HTML. É uma aproximação de aplicação e inclui envio/processamento do HTML.
- Banda estimada: `bytes enviados × 8 / (tempo de envio × 1000)`, em kbps. Reflete a transferência observada pelo servidor, inclusive pacing.
- Ambiguidade: o pareamento é por IP para permitir objetos em outra conexão. Requisições HTML simultâneas do mesmo IP fazem a mais recente substituir a medição pendente.

## Avaliação experimental

Descrever máquinas, SO, topologia, interfaces, arquivos, taxas e repetição. Referenciar T05–T10, tcpdump/Wireshark e IPTraf-ng.

- Ethernet: `[INSERIR RESULTADO REAL]`
- Wi-Fi: `[INSERIR RESULTADO REAL]`
- RTT e banda: `[INSERIR RESULTADO REAL]`
- Estabilidade: `[INSERIR RESULTADO REAL]`

## Conclusões

Comparar resultados reais, limitações da estimativa e trabalhos futuros. `[INSERIR RESULTADO REAL]`

## Referências

Inserir especificação HTTP, documentação POSIX utilizada e referências externas/IA conforme as regras da disciplina.
