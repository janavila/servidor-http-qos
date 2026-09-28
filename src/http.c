#include "http.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

static char *find_header_end(char *data, size_t length) {
    size_t i;

    for (i = 0; i + 3 < length; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            return data + i;
        }
    }
    return NULL;
}

static int header_connection_is(const char *headers, const char *expected) {
    const char *line = strstr(headers, "\r\n") + 2;

    while (line != NULL && *line != '\0') {
        const char *end = strstr(line, "\r\n");
        size_t length;

        if (end == NULL || end == line) {
            break;
        }
        length = (size_t)(end - line);
        if (length >= 11 && strncasecmp(line, "Connection:", 11) == 0) {
            const char *value = line + 11;
            while (value < end && isspace((unsigned char)*value)) {
                value++;
            }
            size_t expected_length = strlen(expected);
            if ((size_t)(end - value) == expected_length &&
                strncasecmp(value, expected, expected_length) == 0) {
                return 1;
            }
        }
        line = end + 2;
    }
    return 0;
}

int http_read_request(int socket_fd, HttpConnectionBuffer *buffer,
                      HttpRequest *request) {
    char *header_end;
    size_t request_length;
    int fields;

    while ((header_end = find_header_end(buffer->data, buffer->length)) == NULL) {
        ssize_t received;
        size_t available = sizeof(buffer->data) - buffer->length;

        if (available == 0) {
            return -2;
        }
        received = recv(socket_fd, buffer->data + buffer->length, available, 0);
        if (received == 0) {
            return 0;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            return -1;
        }
        buffer->length += (size_t)received;
    }

    request_length = (size_t)(header_end - buffer->data) + 4;
    /* O byte nulo e colocado temporariamente apenas para o parser de texto. */
    if (request_length == sizeof(buffer->data)) {
        return -2;
    }
    buffer->data[request_length - 2] = '\0';

    fields = sscanf(buffer->data, "%15s %2047s %15s",
                    request->method, request->path, request->version);
    if (fields != 3 || strchr(request->path, '\r') != NULL ||
        strchr(request->path, '\n') != NULL) {
        return -3;
    }

    request->keep_alive = (strcmp(request->version, "HTTP/1.1") == 0);
    if (header_connection_is(buffer->data, "keep-alive")) {
        request->keep_alive = 1;
    }
    if (header_connection_is(buffer->data, "close")) {
        request->keep_alive = 0;
    }

    memmove(buffer->data, buffer->data + request_length,
            buffer->length - request_length);
    buffer->length -= request_length;
    return 1;
}

int send_all(int socket_fd, const void *data, size_t length) {
    const char *bytes = data;
    size_t sent_total = 0;

    while (sent_total < length) {
        ssize_t sent = send(socket_fd, bytes + sent_total,
                            length - sent_total, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send");
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

int http_send_headers(int socket_fd, int status, const char *reason,
                      const char *content_type, size_t content_length,
                      int keep_alive) {
    char headers[512];
    int length = snprintf(headers, sizeof(headers),
                          "HTTP/1.1 %d %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: %s\r\n"
                          "Server: servidor-mvp1\r\n\r\n",
                          status, reason, content_type, content_length,
                          keep_alive ? "keep-alive" : "close");

    if (length < 0 || (size_t)length >= sizeof(headers)) {
        return -1;
    }
    return send_all(socket_fd, headers, (size_t)length);
}

int http_send_error(int socket_fd, int status, const char *reason,
                    const char *message, int keep_alive) {
    char body[512];
    int length = snprintf(body, sizeof(body),
                          "<!doctype html><html lang=\"pt-BR\"><meta charset=\"utf-8\">"
                          "<title>%d %s</title><h1>%d %s</h1><p>%s</p></html>",
                          status, reason, status, reason, message);

    if (length < 0 || (size_t)length >= sizeof(body)) {
        return -1;
    }
    if (http_send_headers(socket_fd, status, reason, "text/html; charset=utf-8",
                          (size_t)length, keep_alive) < 0) {
        return -1;
    }
    return send_all(socket_fd, body, (size_t)length);
}
