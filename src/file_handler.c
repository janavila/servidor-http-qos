#include "file_handler.h"

#include "http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define DOCUMENT_ROOT "www"
#define FILE_BUFFER_SIZE 16384

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

int serve_static_file(int socket_fd, const char *url_path, int keep_alive,
                      int *status_code) {
    char disk_path[4096];
    char buffer[FILE_BUFFER_SIZE];
    const char *relative_path;
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

    if (http_send_headers(socket_fd, 200, "OK", get_mime_type(disk_path),
                          (size_t)file_info.st_size, keep_alive) < 0) {
        fclose(file);
        return -1;
    }

    while (!feof(file)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
        if (bytes_read > 0 && send_all(socket_fd, buffer, bytes_read) < 0) {
            fclose(file);
            return -1;
        }
        if (ferror(file)) {
            perror("fread");
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    *status_code = 200;
    return 0;
}
