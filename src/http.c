#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define CHUNK_SIZE (64 * 1024)

const char *status_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 206: return "Partial Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

// send() may accept only part of what you give it, so keep going
// until everything is out. Returns false if the connection died.
bool send_all(int client, const void *data, size_t len) {
    const char *p = data;              // where the unsent part starts
    while (len > 0) {
        ssize_t sent = send(client, p, len, MSG_NOSIGNAL);
        if (sent <= 0) return false;   // browser closed the tab, etc.
        p += sent;                     // skip what was sent
        len -= sent;                   // and count it as done
    }
    return true;
}

// Only the header part of a response. The body is sent separately,
// which lets serve_file announce a file's size before reading it.
// extra holds additional header lines, each ending in \r\n ("" for none).
bool send_headers(int client, int status, const char *content_type,
                  size_t content_length, const char *extra) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status, status_reason(status), content_type, content_length, extra);
    if (header_len < 0 || header_len >= (int)sizeof(header)) return false;
    return send_all(client, header, header_len);
}

void send_response(int client, int status, const char *content_type,
                   const void *body, size_t body_len) {
    if (send_headers(client, status, content_type, body_len, "") && body_len > 0)
        send_all(client, body, body_len);
}

void send_text(int client, int status, const char *text) {
    send_response(client, status, "text/plain; charset=utf-8", text, strlen(text));
}

static bool ends_with(const char *s, const char *suffix) {
    size_t s_len = strlen(s), suffix_len = strlen(suffix);
    if (suffix_len > s_len) return false;
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

static const char *content_type_for(const char *path) {
    if (ends_with(path, ".jpg")) return "image/jpeg";
    if (ends_with(path, ".mp4")) return "video/mp4";
    return "application/octet-stream";
}

// Reads a Range header value like "bytes=100-199" or "bytes=100-".
// On success, fills start/end (both inclusive) and returns true.
// Anything unusable returns false, and the whole file is sent instead.
static bool parse_range(const char *range, long long size, long long *start, long long *end) {
    if (range == NULL) return false;

    int got = sscanf(range, "bytes=%lld-%lld", start, end);
    if (got <= 0) return false;            // 0 = no numbers, EOF = empty
    if (got == 1) *end = size - 1;         // "bytes=100-" means "to the end"

    if (*start < 0 || *start >= size) return false;
    if (*end >= size) *end = size - 1;     // browsers sometimes ask past the end
    if (*end < *start) return false;
    return true;
}

// The file name without its folders: "a/b/c.jpg" -> "c.jpg".
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// rel is the part of the URL after "/media/",
// e.g. "photo/NPXS40087/PPSA01234/abc/20260101_120000_00000001.jpg"
void serve_file(int client, const char *root, const char *rel,
                const char *range, bool download) {
    // 1. Refuse anything that could climb out of root.
    if (rel[0] == '/' || strstr(rel, "..") != NULL) {
        send_text(client, 404, "Not found");
        return;
    }

    // 2. Turn the URL part into a real path on disk.
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", root, rel);

    // 3. Does it exist, is it a normal file, and how big is it?
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_text(client, 404, "Not found");
        return;
    }

    FILE *f = fopen(full, "rb");
    if (f == NULL) {
        send_text(client, 404, "Not found");
        return;
    }

    // 4. Whole file (200) or just a part of it (206)?
    long long size = st.st_size;
    long long start = 0, end = size - 1;
    int status = 200;
    char extra[512];
    int extra_len = snprintf(extra, sizeof(extra), "Accept-Ranges: bytes\r\n");

    if (parse_range(range, size, &start, &end)) {
        status = 206;
        extra_len += snprintf(extra + extra_len, sizeof(extra) - extra_len,
                              "Content-Range: bytes %lld-%lld/%lld\r\n", start, end, size);
        // Jump to byte `start` in the file.
        if (fseeko(f, start, SEEK_SET) != 0) {
            fclose(f);
            send_text(client, 500, "Seek failed");
            return;
        }
    }
    if (download) {
        snprintf(extra + extra_len, sizeof(extra) - extra_len,
                 "Content-Disposition: attachment; filename=\"%s\"\r\n", base_name(full));
    }

    long long remaining = end - start + 1;   // +1 because end is inclusive
    if (!send_headers(client, status, content_type_for(full), (size_t)remaining, extra)) {
        fclose(f);
        return;
    }

    // 5. Then the bytes, 64 KB at a time. Each connection runs in its own
    // thread, so each one needs its own buffer (a static one would be shared).
    char *chunk = malloc(CHUNK_SIZE);
    if (chunk == NULL) {
        fclose(f);
        return;
    }
    while (remaining > 0) {
        size_t want = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;
        size_t n = fread(chunk, 1, want, f);
        if (n == 0) break;
        if (!send_all(client, chunk, n)) break;
        remaining -= n;
    }
    free(chunk);
    fclose(f);
}
