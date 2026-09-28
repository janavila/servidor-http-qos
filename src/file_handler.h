#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

int serve_static_file(int socket_fd, const char *url_path, int keep_alive,
                      int *status_code);
const char *get_mime_type(const char *path);

#endif
