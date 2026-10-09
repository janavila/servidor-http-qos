#include "file_handler.h"

#include "http.h"
#include "qos_config.h"
#include "qos_conn_tracker.h"
#include "qos_throttling.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define DOCUMENT_ROOT "www"

const char *get_mime_type(const char *path) {
    const char *extension = strrchr(path, '.');

    if (extension == NULL) return "application/octet-stream";
    if (strcasecmp(extension, ".html") == 0 || strcasecmp(extension, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(extension, ".png") == 0) return "image/png";
    if (strcasecmp(extension, ".gif") == 0) return "image/gif";
    if (strcasecmp(extension, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(extension, ".js") == 0) return "application/javascript";
    if (strcasecmp(extension, ".txt") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static int path_is_safe(const char *url_path) {
    return url_path[0] == '/' && strstr(url_path, "..") == NULL &&
           strchr(url_path, '\\') == NULL && strchr(url_path, '?') == NULL &&
           strchr(url_path, '#') == NULL;
}

/* Contexto do callback de taxa: a taxa da conexao e reavaliada a cada bloco,
 * entao muda sozinha quando outras transferencias do mesmo IP comecam ou terminam. */
typedef struct {
    const char *client_ip;
} RateContext;

static uint32_t current_rate_kbps(void *context) {
    const RateContext *rate = context;
    return qos_effective_rate_kbps(rate->client_ip,
                                   qos_transfer_count(rate->client_ip));
}

int serve_static_file(int socket_fd, const char *url_path, int keep_alive,
                      int *status_code, const char *client_ip) {
    char disk_path[4096];
    const char *relative_path;
    const char *mime_type;
    struct stat file_info;
    FILE *file;

    if (!path_is_safe(url_path)) {
        *status_code = 403;
        return http_send_error(socket_fd, 403, "Forbidden",
                               "O caminho solicitado nao e permitido.", keep_alive);
    }

    relative_path = strcmp(url_path, "/") == 0 ? "index.html" : url_path + 1;
    if (snprintf(disk_path, sizeof(disk_path), "%s/%s", DOCUMENT_ROOT,
                 relative_path) >= (int)sizeof(disk_path)) {
        *status_code = 414;
        return http_send_error(socket_fd, 414, "URI Too Long",
                               "O caminho solicitado e muito longo.", keep_alive);
    }

    if (stat(disk_path, &file_info) < 0 || !S_ISREG(file_info.st_mode)) {
        *status_code = 404;
        return http_send_error(socket_fd, 404, "Not Found",
                               "O arquivo solicitado nao foi encontrado.", keep_alive);
    }

    file = fopen(disk_path, "rb");
    if (file == NULL) {
        perror("fopen");
        *status_code = (errno == EACCES) ? 403 : 500;
        return http_send_error(socket_fd, *status_code,
                               *status_code == 403 ? "Forbidden" : "Internal Server Error",
                               "Nao foi possivel abrir o arquivo.", keep_alive);
    }

    mime_type = get_mime_type(disk_path);
    if (http_send_headers(socket_fd, 200, "OK", mime_type,
                          (size_t)file_info.st_size, keep_alive) < 0) {
        fclose(file);
        return -1;
    }

    {
        /* HTML e isento de limitacao (e nao conta na divisao da taxa);
         * os demais objetos sao enviados com pacing. */
        RateContext rate = {client_ip};
        int limited = !qos_mime_is_exempt(mime_type);
        int tracked = limited && qos_transfer_begin(client_ip);
        ssize_t sent = qos_send_file_throttled_dyn(socket_fd, file,
                                                   (size_t)file_info.st_size,
                                                   mime_type, current_rate_kbps, &rate);
        int saved_errno = errno;

        if (tracked) qos_transfer_end(client_ip);
        fclose(file);

        if (sent < 0) {
            if (saved_errno != EPIPE && saved_errno != ECONNRESET) {
                errno = saved_errno;
                perror("qos_send_file");
            }
            return -1;
        }
    }

    *status_code = 200;
    return 0;
}
