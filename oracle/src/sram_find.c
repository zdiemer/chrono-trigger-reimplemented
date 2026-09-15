#include "sram_find.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trace.h"

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define SRAM_SIZE 8192
#define MAX_DEPTH 3

static int has_srm_extension(const char *name) {
    size_t len = strlen(name);
    if (len < 4)
        return 0;
    const char *ext = name + len - 4;
    return ext[0] == '.' && (ext[1] | 0x20) == 's' && (ext[2] | 0x20) == 'r' && (ext[3] | 0x20) == 'm';
}

static int matches(const char *path, uint32_t crc) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint8_t buf[SRAM_SIZE + 1];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n == SRAM_SIZE && ct_crc32(buf, SRAM_SIZE) == crc;
}

/* Depth-first search of dir for a matching save; 0 with its path in out. */
static int search(const char *dir, uint32_t crc, int depth, char *out, size_t n) {
    char path[1024];
#ifdef _WIN32
    if ((size_t)snprintf(path, sizeof path, "%s/*", dir) >= sizeof path)
        return -1;
    struct _finddata_t fd;
    intptr_t h = _findfirst(path, &fd);
    if (h == -1)
        return -1;
    int found = -1;
    do {
        if (!strcmp(fd.name, ".") || !strcmp(fd.name, ".."))
            continue;
        if ((size_t)snprintf(path, sizeof path, "%s/%s", dir, fd.name) >= sizeof path)
            continue;
        if (fd.attrib & _A_SUBDIR) {
            if (depth < MAX_DEPTH && search(path, crc, depth + 1, out, n) == 0)
                found = 0;
        } else if (has_srm_extension(fd.name) && fd.size == SRAM_SIZE && matches(path, crc)) {
            snprintf(out, n, "%s", path);
            found = 0;
        }
    } while (found != 0 && _findnext(h, &fd) == 0);
    _findclose(h);
    return found;
#else
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int found = -1;
    struct dirent *e;
    while (found != 0 && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if ((size_t)snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= sizeof path)
            continue;
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH && search(path, crc, depth + 1, out, n) == 0)
                found = 0;
        } else if (has_srm_extension(e->d_name) && st.st_size == SRAM_SIZE && matches(path, crc)) {
            snprintf(out, n, "%s", path);
            found = 0;
        }
    }
    closedir(d);
    return found;
#endif
}

/* The directory part of path ("." if none), with `suffix` appended. */
static void dir_of(const char *path, const char *suffix, char *out, size_t n) {
    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            slash = p;
    }
    if (slash)
        snprintf(out, n, "%.*s%s", (int)(slash - path), path, suffix);
    else
        snprintf(out, n, ".%s", suffix);
}

int ct_sram_find(uint32_t crc, const char *movie_path, const char *rom_path, char *out, size_t n) {
    char dir[1024];
    if (movie_path) {
        static const char *const rel[] = { "/../saves", "/../../saves", "/saves" };
        for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++) {
            dir_of(movie_path, rel[i], dir, sizeof dir);
            if (search(dir, crc, 0, out, n) == 0)
                return 0;
        }
    }
    if (rom_path) {
        dir_of(rom_path, "", dir, sizeof dir);
        if (search(dir, crc, 0, out, n) == 0)
            return 0;
    }
    return -1;
}
