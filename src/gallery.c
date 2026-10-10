#include "gallery.h"
#include "buffer.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Captures live at  <root>/<kind>/NPXS40087/<TitleID>/<dir>/<file>
// where kind is "photo" or "video". Next to every .jpg/.mp4 there's a
// .dat file that holds the game's name (at offset 0x44), e.g.
//   "ASTRO's PLAYROOM_20260319211421"

#define DAT_NAME_OFFSET 0x44
#define DAT_NAME_MAX    256
#define MAX_TITLES      512

typedef struct {
    Buffer *captures;                 // "path","path",...
    Buffer *titles;                   // "ID":"Name","ID":"Name",...
    char seen[MAX_TITLES][16];        // title IDs whose name we already have
    int seen_count;
} Scan;

static bool ends_with(const char *s, const char *suffix)
{
    const size_t suffix_len = strlen(suffix);
    const size_t s_len = strlen(s);
    if (suffix_len > s_len) return false;
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

// Appends s as a JSON string, quotes included. Game names can contain
// quotes or backslashes, which would otherwise break the JSON.
static void append_json_string(Buffer *out, const char *s)
{
    buf_append(out, "\"");
    for (; *s; s++) {
        char tmp[8];
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            tmp[0] = '\\'; tmp[1] = (char)c; tmp[2] = '\0';
        } else if (c < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", c);
        } else {
            tmp[0] = (char)c; tmp[1] = '\0';   // UTF-8 bytes pass through as-is
        }
        buf_append(out, tmp);
    }
    buf_append(out, "\"");
}

static bool title_seen(const Scan *scan, const char *title_id)
{
    for (int i = 0; i < scan->seen_count; i++)
        if (strcmp(scan->seen[i], title_id) == 0) return true;
    return false;
}

// Reads the game name out of a .dat file and drops the "_YYYYMMDDHHMMSS"
// at its end. Returns false if there's no usable name.
static bool read_dat_name(const char *path, char *name, size_t name_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;

    char raw[DAT_NAME_MAX + 1] = {0};
    bool ok = fseek(f, DAT_NAME_OFFSET, SEEK_SET) == 0 &&
              fread(raw, 1, DAT_NAME_MAX, f) > 0;
    fclose(f);
    if (!ok || raw[0] == '\0') return false;

    char *underscore = strrchr(raw, '_');
    if (underscore != NULL && strlen(underscore + 1) == 14 &&
        strspn(underscore + 1, "0123456789") == 14) {
        *underscore = '\0';
    }

    snprintf(name, name_size, "%s", raw);
    return name[0] != '\0';
}

// rel looks like "photo/NPXS40087/PPSA01325/011/x.dat"; the title ID is part 3.
static void title_id_from_rel(const char *rel, char *out, size_t out_size)
{
    out[0] = '\0';
    char id[16];
    if (sscanf(rel, "%*[^/]/%*[^/]/%15[^/]", id) == 1)
        snprintf(out, out_size, "%s", id);
}

static void walk(const char *root, const char *rel, int depth, Scan *scan)
{
    char full[512];
    snprintf(full, sizeof(full), "%s/%s", root, rel);

    DIR *dir = opendir(full);
    if (dir == NULL) {
        if (depth == 0) perror(full);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char child[512];
        snprintf(child, sizeof(child), "%s/%s", rel, name);

        if (depth < 3) {
            walk(root, child, depth + 1, scan);
        } else if (ends_with(name, ".jpg") || ends_with(name, ".mp4")) {
            if (scan->captures->len > 0) buf_append(scan->captures, ",");
            append_json_string(scan->captures, child);
        } else if (ends_with(name, ".dat")) {
            // One name per game is enough, so only the first .dat is read.
            char title_id[16];
            title_id_from_rel(child, title_id, sizeof(title_id));
            if (title_id[0] == '\0' || title_seen(scan, title_id) ||
                scan->seen_count >= MAX_TITLES)
                continue;

            char dat_path[1024], game_name[DAT_NAME_MAX + 1];
            snprintf(dat_path, sizeof(dat_path), "%s/%s", root, child);
            if (!read_dat_name(dat_path, game_name, sizeof(game_name)))
                continue;

            snprintf(scan->seen[scan->seen_count++], sizeof(scan->seen[0]), "%s", title_id);
            if (scan->titles->len > 0) buf_append(scan->titles, ",");
            append_json_string(scan->titles, title_id);
            buf_append(scan->titles, ":");
            append_json_string(scan->titles, game_name);
        }
    }

    closedir(dir);
}

void scan_captures(const char *root, Buffer *out)
{
    Buffer captures, titles;
    buf_init(&captures);
    buf_init(&titles);

    Scan scan = { .captures = &captures, .titles = &titles, .seen_count = 0 };
    walk(root, "photo", 0, &scan);
    walk(root, "video", 0, &scan);

    // {"titles":{"ID":"Name",...},"captures":["path",...]}
    buf_append(out, "{\"titles\":{");
    if (titles.data) buf_append(out, titles.data);
    buf_append(out, "},\"captures\":[");
    if (captures.data) buf_append(out, captures.data);
    buf_append(out, "]}");

    buf_free(&captures);
    buf_free(&titles);
}
