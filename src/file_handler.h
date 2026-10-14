#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <stddef.h>

typedef struct {
    int is_html;
    size_t bytes_sent;
    double transfer_seconds;
} StaticFileResult;

int serve_static_file(int socket_fd, const char *url_path, int keep_alive,
                      int *status_code, const char *client_ip,
                      StaticFileResult *file_result);
const char *get_mime_type(const char *path);

#endif
