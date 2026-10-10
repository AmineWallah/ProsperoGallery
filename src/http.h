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
// Sends root/rel. range is the Range header's value (NULL if none),
// download makes the browser save it, and original skips the JXR -> JPG
// conversion for HDR screenshots.
void serve_file(int client, const char *root, const char *rel,
                const char *range, bool download, bool original);

// Sends the file at full. download_name: save it under that name (NULL = show it).
void serve_path(int client, const char *full, const char *range, const char *download_name);

#endif
