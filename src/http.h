#ifndef HTTP_H
#define HTTP_H

#include <stdbool.h>
#include <stddef.h>

const char *status_reason(int status);
bool send_all(int client, const void *data, size_t len);
bool send_headers(int client, int status, const char *content_type,
                  size_t content_length, const char *extra);
void send_response(int client, int status, const char *content_type,
                   const void *body, size_t body_len);
void send_text(int client, int status, const char *text);
void serve_file(int client, const char *root, const char *rel,
                const char *range, bool download);

#endif
