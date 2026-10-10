#include "launcher.h"

#ifndef PS5

int launcher_install(int port)
{
    (void)port;
    return 0;   // there's no home screen on PC
}

#else

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ps5/kernel.h>

#include "assets_icon0_png.h"   // generated from assets/icon0.png by the Makefile

// The icon is a regular installed "app" with no program inside: just a
// param.json and an icon in /user/app/<TitleID>/sce_sys/. Two fields in
// param.json do the work:
//   applicationCategoryType 65536  puts it in the Media tab instead of Games
//   deeplinkUri                    is what the PS5 opens when you select it
// Same approach as itsPLK's PS5 Payload Manager and John Törnblom's ftpsrv.

int sceUserServiceInitialize(void *);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);

#define APP_DIR "/user/app/" LAUNCHER_TITLE_ID

// True if the file at path already contains exactly data[0..size).
static bool file_matches(const char *path, const void *data, size_t size)
{
    struct stat st;
    if (stat(path, &st) != 0 || (size_t)st.st_size != size) return false;

    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    char *buf = malloc(size);
    bool same = buf != NULL && fread(buf, 1, size, f) == size &&
                memcmp(buf, data, size) == 0;
    free(buf);
    fclose(f);
    return same;
}

// sceAppInstUtilAppInstallTitleDir isn't exported on every firmware, so it's
// looked up at runtime (by its NID) instead of linked. If it's missing,
// InstallAll does the same job by rescanning every folder in /user/app.
// Same approach as Payload Manager and ftpsrv.
static int install_title_dir(const char *title_id, const char *dir)
{
    int (*install_dir)(const char *, const char *, void *) = NULL;
    uint32_t handle;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        install_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");

    if (install_dir != NULL)
        return install_dir(title_id, dir, NULL);
    return sceAppInstUtilAppInstallAll(NULL);
}

static bool write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

int launcher_install(int port)
{
    char param[512];
    int param_len = snprintf(param, sizeof(param),
        "{\n"
        "    \"titleId\": \"" LAUNCHER_TITLE_ID "\",\n"
        "    \"applicationCategoryType\": 65536,\n"
        "    \"deeplinkUri\": \"http://127.0.0.1:%d/connect\",\n"
        "    \"localizedParameters\": {\n"
        "        \"defaultLanguage\": \"en-US\",\n"
        "        \"en-US\": { \"titleName\": \"ProsperoGallery\" }\n"
        "    }\n"
        "}\n", port);

    const char *param_path = APP_DIR "/sce_sys/param.json";
    const char *icon_path  = APP_DIR "/sce_sys/icon0.png";

    if (file_matches(param_path, param, param_len) &&
        file_matches(icon_path, assets_icon0_png, assets_icon0_png_len))
        return 0;   // installed by an earlier run, nothing changed

    int prio = 256;
    sceUserServiceInitialize(&prio);   // fails harmlessly if already initialized

    if (sceAppInstUtilInitialize() != 0) return -1;

    int result = -1;
    if ((mkdir(APP_DIR, 0755) == 0 || errno == EEXIST) &&
        (mkdir(APP_DIR "/sce_sys", 0755) == 0 || errno == EEXIST) &&
        write_file(param_path, param, param_len) &&
        write_file(icon_path, assets_icon0_png, assets_icon0_png_len) &&
        install_title_dir(LAUNCHER_TITLE_ID, "/user/app/") == 0) {
        result = 1;
    }

    sceAppInstUtilTerminate();
    return result;
}

#endif
