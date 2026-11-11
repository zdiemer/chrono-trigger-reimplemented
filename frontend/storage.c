#include "storage.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

uint8_t *files_read(const char *path, size_t *len, bool text, bool *missing)
{
    if (missing)
        *missing = false;
    FILE *f = fopen(path, text ? "r" : "rb");
    if (!f) {
        if (missing)
            *missing = errno == ENOENT;
        return NULL;
    }
    uint8_t *data = NULL;
    size_t n = 0, cap = 0, got;
    do {
        if (n == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *grown = realloc(data, cap);
            if (!grown) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = grown;
        }
        got = fread(data + n, 1, cap - n, f);
        n += got;
    } while (got);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) {
        free(data);
        return NULL;
    }
    *len = n;
    return data;
}

int files_write(const char *path, const void *data, size_t len, bool text)
{
    FILE *f = fopen(path, text ? "w" : "wb");
    int ok = f && fwrite(data, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = 0;
    if (!ok) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    return 0;
}

const char *files_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
}

void files_side_path(char *out, size_t n, const char *name_path, const char *dir_of, const char *dir, const char *ext)
{
    const char *name = files_name(name_path);
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (dir)
        snprintf(out, n, "%s/%.*s%s", dir, stem, name, ext);
    else
        snprintf(out, n, "%.*s%.*s%s", (int)(files_name(dir_of) - dir_of), dir_of, stem, name, ext);
}

bool files_absolute(char *out, size_t size, const char *path)
{
    int n;
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':'))
        n = snprintf(out, size, "%s", path);
    else {
        char cwd[2048];
        if (!getcwd(cwd, sizeof cwd)) return false;
        n = snprintf(out, size, "%s/%s", cwd, path);
    }
    return n >= 0 && (size_t)n < size;
}
