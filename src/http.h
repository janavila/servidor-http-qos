#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_REQUEST_MAX 16384
#define HTTP_METHOD_MAX 16
#define HTTP_PATH_MAX 2048
#define HTTP_VERSION_MAX 16

typedef struct {
    char method[HTTP_METHOD_MAX];
    char path[HTTP_PATH_MAX];
    char version[HTTP_VERSION_MAX];
    int keep_alive;
} HttpRequest;

/* Guarda dados que chegaram adiantados para a proxima requisicao keep-alive. */
typedef struct {
    char data[HTTP_REQUEST_MAX];
    size_t length;
} HttpConnectionBuffer;

int http_read_request(int socket_fd, HttpConnectionBuffer *buffer,
                      HttpRequest *request);
int http_send_headers(int socket_fd, int status, const char *reason,
                      const char *content_type, size_t content_length,
                      int keep_alive);
int http_send_error(int socket_fd, int status, const char *reason,
                    const char *message, int keep_alive);
int send_all(int socket_fd, const void *data, size_t length);

#endif
