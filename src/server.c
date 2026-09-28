#include "file_handler.h"
#include "http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 8080
#define LISTEN_BACKLOG 64

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
} ClientContext;

static int parse_port(const char *text, unsigned short *port) {
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || value < 1 || value > 65535) {
        return -1;
    }
    *port = (unsigned short)value;
    return 0;
}

static int create_server_socket(unsigned short port) {
    int socket_fd;
    int enable = 1;
    struct sockaddr_in address;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return -1;
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(socket_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(socket_fd);
        return -1;
    }
    if (listen(socket_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(socket_fd);
        return -1;
    }
    return socket_fd;
}

static void *handle_client(void *argument) {
    ClientContext *client = argument;
    HttpConnectionBuffer buffer = {{0}, 0};
    char client_ip[INET_ADDRSTRLEN] = "desconhecido";

    inet_ntop(AF_INET, &client->address.sin_addr, client_ip, sizeof(client_ip));
    printf("Cliente conectado: %s:%u\n", client_ip, ntohs(client->address.sin_port));

    for (;;) {
        HttpRequest request;
        int result = http_read_request(client->socket_fd, &buffer, &request);
        int status = 0;

        if (result == 0 || result == -1) break;
        if (result == -2) {
            http_send_error(client->socket_fd, 431, "Request Header Fields Too Large",
                            "A requisicao ultrapassou o limite de 16 KiB.", 0);
            break;
        }
        if (result == -3) {
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
            result = serve_static_file(client->socket_fd, request.path,
                                       request.keep_alive, &status);
        }

        printf("%d %s\n", status,
               status == 200 ? "OK" : status == 404 ? "Not Found" : "Erro");
        if (result < 0 || !request.keep_alive) break;
    }

    close(client->socket_fd);
    printf("Cliente desconectado: %s\n", client_ip);
    free(client);
    return NULL;
}

int main(int argc, char **argv) {
    unsigned short port = DEFAULT_PORT;
    int server_fd;

    if (argc > 2 || (argc == 2 && parse_port(argv[1], &port) < 0)) {
        fprintf(stderr, "Uso: %s [porta de 1 a 65535]\n", argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    server_fd = create_server_socket(port);
    if (server_fd < 0) return EXIT_FAILURE;

    printf("Servidor iniciado na porta %u\n", port);
    fflush(stdout);
    for (;;) {
        ClientContext *client = malloc(sizeof(*client));
        socklen_t address_length;
        pthread_t thread;

        if (client == NULL) {
            perror("malloc");
            continue;
        }
        address_length = sizeof(client->address);
        client->socket_fd = accept(server_fd, (struct sockaddr *)&client->address,
                                   &address_length);
        if (client->socket_fd < 0) {
            if (errno != EINTR) perror("accept");
            free(client);
            continue;
        }
        if (pthread_create(&thread, NULL, handle_client, client) != 0) {
            fprintf(stderr, "pthread_create: nao foi possivel criar a thread\n");
            close(client->socket_fd);
            free(client);
            continue;
        }
        if (pthread_detach(thread) != 0) {
            fprintf(stderr, "pthread_detach: nao foi possivel destacar a thread\n");
        }
    }
}
