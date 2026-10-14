#define _POSIX_C_SOURCE 200809L

#include "file_handler.h"
#include "http.h"
#include "qos_config.h"
#include "qos_conn_tracker.h"
#include "qos_metrics.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define DEFAULT_PORT 8080
#define LISTEN_BACKLOG 64

typedef struct {
    int socket_fd; //valor do socket do cliente
    struct sockaddr_in address; // endereço completo IP e porta do cliente
    char ip[INET_ADDRSTRLEN];
} ClientContext;

static int parse_port(const char *text, unsigned short *port) { // Lê a porta que eu vou passar pro terminal
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10); // converte a string para um inteiro de 10 bits
    if (errno != 0 || *text == '\0' || *end != '\0' || value < 1 || value > 65535) { // Verifica se entá dentro de um valor vaílido de TCP
        return -1;
    }
    *port = (unsigned short)value; // salva o valor
    return 0;
}

static int parse_throughput(const char *text, uint64_t *kbps) {
    char *end;
    unsigned long long value;

    if (text == NULL || *text == '\0' || *text == '-') return -1;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value == 0) return -1;
    *kbps = (uint64_t)value;
    return 0;
}

static int create_server_socket(unsigned short port) { //cria e prepara o socket servidor
    int socket_fd;
    int enable = 1;
    struct sockaddr_in address;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0); //socket IPv4 usando TCP
    if (socket_fd < 0) {
        perror(" valor de socket invalido");
        return -1;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(socket_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // permite a conexão com qualquer interface da máquina
    address.sin_port = htons(port); // converte a porta para o formato de rede

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { // associa o socket a porta e endereço
        perror("bind");
        close(socket_fd);
        return -1;
    }
    if (listen(socket_fd, LISTEN_BACKLOG) < 0) { // para receber conexões TCP
        perror("listen");
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static void *metrics_panel(void *unused)
{
    (void)unused;
    const struct timespec interval = {2, 0};

    for (;;) {
        QosConnectionSnapshot clients[QOS_CONNECTIONS_MAX_IPS];
        uint64_t allocated = 0, maximum = 0;
        size_t count;

        nanosleep(&interval, NULL);
        count = qos_connections_snapshot(clients, QOS_CONNECTIONS_MAX_IPS,
                                         &allocated, &maximum);
        printf("\n============================================================\n");
        printf("Servidor HTTP/1.1 + QoS | Vazao: %llu / %llu kbps\n",
               (unsigned long long)allocated, (unsigned long long)maximum);
        printf("------------------------------------------------------------\n");
        printf("%-15s %5s %9s %10s %10s %12s\n",
               "IP", "Con.", "Taxa IP", "Taxa/Conn", "RTT est.", "Banda est.");
        for (size_t i = 0; i < count; i++) {
            QosMetricsSnapshot metrics;
            int found = qos_metrics_get(clients[i].ip, &metrics);
            char rtt[24] = "--", bandwidth[24] = "--";
            if (found && metrics.has_rtt)
                snprintf(rtt, sizeof rtt, "%.2f ms", metrics.estimated_rtt_ms);
            if (found && metrics.has_bandwidth)
                snprintf(bandwidth, sizeof bandwidth, "%.2f kbps",
                         metrics.estimated_bandwidth_kbps);
            printf("%-15s %5d %9u %10u %10s %12s\n",
                   clients[i].ip, clients[i].connections_for_ip,
                   clients[i].configured_rate_kbps, clients[i].effective_rate_kbps,
                   rtt, bandwidth);
        }
        if (count == 0)
            printf("Nenhum cliente ativo.\n");
        printf("============================================================\n");
    }
    return NULL;
}

static void *handle_client(void *argument) {
    ClientContext *client = argument;
    HttpConnectionBuffer buffer = {{0}, 0};
    QosConnectionState state;
    char client_ip[INET_ADDRSTRLEN] = "desconhecido";

    inet_ntop(AF_INET, &client->address.sin_addr, client_ip, sizeof(client_ip));// converte o endereço IP do cliente para uma string legível
    printf("Cliente conectado: %s:%u\n", client_ip, ntohs(client->address.sin_port));// converte o número da porta do cliente para o formato de rede e imprime na tela

    for (;;) {
        HttpRequest request;
        struct timespec request_arrival;
        int result = http_read_request(client->socket_fd, &buffer, &request);// lê a requisição HTTP do cliente e armazena no buffer e na estrutura request
        int status = 0;
        int have_arrival = qos_metrics_now(&request_arrival) == 0;

        if (result == 0 || result == -1) break;// se o cliente fechou a conexão ou ocorreu um erro, sai do loop
        if (result == -2) {// se a requisição ultrapassou o limite de 16 KiB, envia um erro 431
            http_send_error(client->socket_fd, 431, "Request Header Fields Too Large",
                            "A requisicao ultrapassou o limite de 16 KiB.", 0);
            break;
        }
        if (result == -3) {// se a requisição for inválida, envia um erro 400
            http_send_error(client->socket_fd, 400, "Bad Request",
                            "A linha de requisicao e invalida.", 0);
            break;
        }

        printf("%s %s\n", request.method, request.path);
        if (strcmp(request.version, "HTTP/1.1") != 0 &&
            strcmp(request.version, "HTTP/1.0") != 0) {
            status = 505;
            result = http_send_error(client->socket_fd, 505,
                                     "HTTP Version Not Supported",
                                     "Use HTTP/1.0 ou HTTP/1.1.", 0);
            request.keep_alive = 0;
        } else if (strcmp(request.method, "GET") != 0) {
            status = 405;
            result = http_send_error(client->socket_fd, 405, "Method Not Allowed",
                                     "Este MVP aceita somente o metodo GET.",
                                     request.keep_alive);
        } else {
            StaticFileResult file_result;
            result = serve_static_file(client->socket_fd, request.path,
                                       request.keep_alive, &status, client->ip,
                                       &file_result);
            if (result == 0 && status == 200 && have_arrival) {
                if (file_result.is_html) {
                    qos_metrics_record_html(client->ip, &request_arrival);
                } else {
                    int rtt_ready = qos_metrics_record_object(client->ip, &request_arrival);
                    qos_metrics_record_transfer(client->ip, file_result.bytes_sent,
                                                file_result.transfer_seconds);
                    if (rtt_ready) {
                        QosMetricsSnapshot metrics;
                        if (qos_metrics_get(client->ip, &metrics))
                            printf("Metricas %s: RTT estimado %.2f ms, banda estimada %.2f kbps\n",
                                   client->ip, metrics.estimated_rtt_ms,
                                   metrics.estimated_bandwidth_kbps);
                    }
                }
            }
        }

        printf("%d %s\n", status,
               status == 200 ? "OK" : status == 404 ? "Not Found" : "Erro");
        if (result < 0 || !request.keep_alive) break;
    }

    if (qos_connection_remove(client->ip, &state) == 0) {
        printf("Cliente removido: %s\n", client->ip);
        printf("Conexoes restantes do IP: %d\n", state.connections_for_ip);
        printf("Vazao global alocada: %llu/%llu kbps\n",
               (unsigned long long)state.global_allocated_kbps,
               (unsigned long long)state.max_global_kbps);
    }
    close(client->socket_fd);
    printf("Cliente desconectado: %s\n", client_ip);
    free(client);
    return NULL;
}

int main(int argc, char **argv) {
    unsigned short port = DEFAULT_PORT;
    uint64_t max_throughput_kbps;
    const char *qos_file;
    int server_fd;
    pthread_t panel_thread;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc != 4 || parse_port(argv[1], &port) < 0 || argv[2][0] == '\0' ||
        parse_throughput(argv[3], &max_throughput_kbps) < 0) {
        fprintf(stderr, "Uso: %s <porta> <arquivo_qos> <vazao_maxima_kbps>\n", argv[0]);
        return EXIT_FAILURE;
    }
    qos_file = argv[2];
    signal(SIGPIPE, SIG_IGN);// comando protege caso o cliente feche a conexão, ele segue em frente e não derruba o sistema todo
    if (qos_load_config(qos_file) != 0) return EXIT_FAILURE;
    if (qos_connections_init(max_throughput_kbps) != 0) {
        fprintf(stderr, "Nao foi possivel inicializar o controle de admissao.\n");
        return EXIT_FAILURE;
    }
    qos_metrics_init();
    server_fd = create_server_socket(port);
    if (server_fd < 0) return EXIT_FAILURE;

    printf("Servidor iniciado na porta %u\n", port);
    printf("Arquivo QoS: %s\n", qos_file);
    printf("Vazao maxima: %llu kbps\n", (unsigned long long)max_throughput_kbps);
    if (pthread_create(&panel_thread, NULL, metrics_panel, NULL) != 0) {
        fprintf(stderr, "pthread_create: nao foi possivel criar o painel\n");
        close(server_fd);
        return EXIT_FAILURE;
    }
    if (pthread_detach(panel_thread) != 0) {
        fprintf(stderr, "pthread_detach: nao foi possivel destacar o painel\n");
        close(server_fd);
        return EXIT_FAILURE;
    }
    fflush(stdout); // esvazia o buffer de saída padrão
    for (;;) {
        QosConnectionState state;
        ClientContext *client = malloc(sizeof(*client)); // reserva memória para guardar informações de um cliente específico
        socklen_t address_length; // tamanho da estrutura de endereço do cliente
        pthread_t thread; // variável para armazenar o identificador da thread que vai tratar o cliente

        if (client == NULL) {
            perror("malloc");
            continue;
        }
        address_length = sizeof(client->address);
        client->socket_fd = accept(server_fd, (struct sockaddr *)&client->address, // espera conexão de algum cliente
                                   &address_length);
        if (client->socket_fd < 0) {
            if (errno != EINTR) perror("accept");
            free(client);
            continue;
        }
        if (inet_ntop(AF_INET, &client->address.sin_addr, client->ip,
                      sizeof client->ip) == NULL) {
            perror("inet_ntop");
            close(client->socket_fd);
            free(client);
            continue;
        }

        int admitted = qos_connection_admit(client->ip, &state);
        if (admitted != 1) {
            if (admitted == 0) {
                printf("Cliente rejeitado: %s\n", client->ip);
                printf("Vazao projetada excede limite: %llu/%llu kbps\n",
                       (unsigned long long)state.global_allocated_kbps,
                       (unsigned long long)state.max_global_kbps);
            } else {
                printf("Cliente rejeitado: %s - estado de conexoes indisponivel\n",
                       client->ip);
            }
            close(client->socket_fd);
            free(client);
            continue;
        }

        printf("Cliente admitido: %s\n", client->ip);
        printf("Conexoes do IP: %d\n", state.connections_for_ip);
        printf("Taxa configurada: %u kbps\n", state.configured_rate_kbps);
        printf("Taxa efetiva: %u kbps\n", state.effective_rate_kbps);
        printf("Vazao global alocada: %llu/%llu kbps\n",
               (unsigned long long)state.global_allocated_kbps,
               (unsigned long long)state.max_global_kbps);
        if (pthread_create(&thread, NULL, handle_client, client) != 0) {
            fprintf(stderr, "pthread_create: nao foi possivel criar a thread\n");
            qos_connection_remove(client->ip, NULL);
            close(client->socket_fd);
            free(client);
            continue;
        }
        if (pthread_detach(thread) != 0) {
            fprintf(stderr, "pthread_detach: nao foi possivel destacar a thread\n");
        }
    }
}
