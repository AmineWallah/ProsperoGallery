#include "http.h"
#include "buffer.h"
#include "gallery.h"
#include "instance.h"
#include "launcher.h"
#include "qr.h"
#include "web_connect_html.h"   // generated from web/ by the Makefile
#include "web_index_html.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// The PS5 build passes -DMEDIA_ROOT="/user/av_contents" (see the Makefile).
#ifndef MEDIA_ROOT
#define MEDIA_ROOT "./test_folder/user/av_contents"
#endif

#define PORT 1337
#define PORT_STR "1337"

// --- Messages: a toast on the PS5, plain stdout on PC ------------------------

#ifdef PS5
typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *message) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    printf("%s\n", message);
}
#else
static void notify(const char *message) {
    printf("%s\n", message);
    fflush(stdout);
}
#endif

// The machine's LAN address (first non-loopback IPv4), so the startup
// message can show the exact URL to open. Falls back to "<ip>".
static void local_ip(char *out, size_t out_size) {
    snprintf(out, out_size, "<ip>");

    struct ifaddrs *list;
    if (getifaddrs(&list) != 0) return;

    for (struct ifaddrs *ifa = list; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        if ((ntohl(addr->sin_addr.s_addr) >> 24) == 127) continue;   // 127.x.x.x
        inet_ntop(AF_INET, &addr->sin_addr, out, out_size);
        break;
    }
    freeifaddrs(list);
}

// "http://192.168.1.20:8080/" - what phones should open. Looked up on every
// request instead of once, in case the console's address changes.
static void gallery_url(char *out, size_t out_size) {
    char ip[INET_ADDRSTRLEN + 8];
    local_ip(ip, sizeof(ip));
    snprintf(out, out_size, "http://%s:%d/", ip, PORT);
}

// --- One request -------------------------------------------------------------

static void handle_client(int client) {
    // A browser that connects and then says nothing shouldn't hold a thread forever.
    struct timeval timeout = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char req[4096];
    ssize_t n = recv(client, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    char method[8], path[512];
    if (sscanf(req, "%7s %511s", method, path) != 2) {
        send_text(client, 400, "Bad request");
        return;
    }

    // "/media/x.mp4?download" -> path "/media/x.mp4", download = true
    bool download = false;
    char *query = strchr(path, '?');
    if (query != NULL) {
        download = strstr(query + 1, "download") != NULL;
        *query = '\0';
    }

    // The value after "Range: ", or NULL if the browser didn't send one.
    // sscanf in parse_range stops at the \r, so the rest of req doesn't matter.
    const char *range = strstr(req, "\r\nRange: ");
    if (range != NULL) range += strlen("\r\nRange: ");

    if (strcmp(method, "GET") != 0) {
        send_text(client, 405, "Method not allowed");
    } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        send_response(client, 200, "text/html; charset=utf-8",
                      web_index_html, web_index_html_len);
    } else if (strcmp(path, "/api/captures") == 0) {
        Buffer b;
        buf_init(&b);
        scan_captures(MEDIA_ROOT, &b);
        if (b.data != NULL)
            send_response(client, 200, "application/json", b.data, b.len);
        else
            send_text(client, 500, "Out of memory");
        buf_free(&b);
    } else if (strncmp(path, "/media/", 7) == 0) {
        serve_file(client, MEDIA_ROOT, path + 7, range, download);
    } else if (strcmp(path, "/connect") == 0) {
        send_response(client, 200, "text/html; charset=utf-8",
                      web_connect_html, web_connect_html_len);
    } else if (strcmp(path, "/api/info") == 0) {
        char url[64], json[128];
        gallery_url(url, sizeof(url));
        int len = snprintf(json, sizeof(json), "{\"url\":\"%s\"}", url);
        send_response(client, 200, "application/json", json, len);
    } else if (strcmp(path, "/qr.svg") == 0) {
        char url[64];
        gallery_url(url, sizeof(url));
        Buffer b;
        buf_init(&b);
        if (qr_svg(url, &b))
            send_response(client, 200, "image/svg+xml", b.data, b.len);
        else
            send_text(client, 500, "QR failed");
        buf_free(&b);
    } else {
        send_text(client, 404, "Not found");
    }
}

// Each connection gets its own thread, so a long video download doesn't
// block the thumbnails (or anyone else) while it runs.
static void *client_thread(void *arg) {
    int client = (int)(intptr_t)arg;
    handle_client(client);
    close(client);
    return NULL;
}

// Installs/updates the Media tab icon. Runs in its own thread after the
// server is already up, so if the console's installer is slow or hangs,
// the gallery still works.
static void *icon_thread(void *arg) {
    (void)arg;
    int result = launcher_install(PORT);
    if (result == 1) notify("Gallery: icon added to the Media tab");
    else if (result < 0) notify("Gallery: couldn't add the Media tab icon");
    return NULL;
}

// --- Main loop ---------------------------------------------------------------

int main(void) {
    // Belt and braces next to MSG_NOSIGNAL: a closed tab must never kill us.
    signal(SIGPIPE, SIG_IGN);

    instance_take_over();   // stops an older copy that's still running
    notify("Gallery starting...");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        notify("Gallery: socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        notify("Gallery: port " PORT_STR " is busy (already running?)");
        close(fd);
        return 1;
    }
    if (listen(fd, 64) != 0) {
        notify("Gallery: listen failed");
        close(fd);
        return 1;
    }

    char url[64], message[128];
    gallery_url(url, sizeof(url));
    snprintf(message, sizeof(message), "Gallery running: %s", url);
    notify(message);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  // cleans up after itself

    pthread_t icon;
    pthread_create(&icon, &attr, icon_thread, NULL);

    while (true) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) continue;

        pthread_t thread;
        if (pthread_create(&thread, &attr, client_thread, (void *)(intptr_t)client) != 0) {
            // Couldn't start a thread: serve it here instead of dropping it.
            client_thread((void *)(intptr_t)client);
        }
    }
}
