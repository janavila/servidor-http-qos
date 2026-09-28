# Servidor HTTP/1.1 Concorrente — MVP1

Projeto da disciplina de **Fundamentos e Avaliação de Redes de Computadores**.

O programa consiste em um servidor HTTP/1.1 desenvolvido em linguagem C, utilizando sockets TCP POSIX e Pthreads para atendimento concorrente de múltiplos clientes.

Esta versão corresponde exclusivamente ao **MVP1** do trabalho.

---

## Integrantes

- Jansen Avila
- Leonardo Borges
- Rodrigo Peraça

---

## Requisitos

Para compilar e executar o projeto é necessário:

- Linux;
- GCC;
- Make;
- biblioteca Pthreads;
- `curl`;
- Python 3 para execução de alguns testes;
- opcionalmente `wget`, Wireshark ou `tcpdump`.

No Ubuntu, as principais dependências podem ser instaladas com:

```bash
sudo apt update
sudo apt install build-essential git curl wget tcpdump