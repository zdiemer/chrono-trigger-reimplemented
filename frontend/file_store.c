#define _POSIX_C_SOURCE 200809L
#include "file_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

/* The bytes written and closed in a file beside `path`, named for this
 * process: its name, malloc'd, or NULL with nothing left behind. */
static char *written_beside(const char *path, const void *bytes, size_t size)
{
    static unsigned serial;
    size_t capacity = strlen(path) + 80;
    char *temporary = malloc(capacity);
    if (!temporary)
        return NULL;
    int fd = -1;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
#ifdef _WIN32
        unsigned long pid = (unsigned long)_getpid();
#else
        unsigned long pid = (unsigned long)getpid();
#endif
        snprintf(temporary, capacity, "%s.tmp.%lu.%u", path, pid, serial++);
#ifdef _WIN32
        fd = _open(temporary, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#endif
        if (fd >= 0 || errno != EEXIST)
            break;
    }
    if (fd < 0) {
        free(temporary);
        return NULL;
    }
#ifdef _WIN32
    FILE *f = _fdopen(fd, "wb");
#else
    FILE *f = fdopen(fd, "wb");
#endif
    int ok = 0;
    if (f) {
        ok = fwrite(bytes, 1, size, f) == size && fflush(f) == 0;
#ifdef _WIN32
        if (ok && _commit(fd) != 0)
#else
        if (ok && fsync(fd) != 0)
#endif
            ok = 0;
        if (fclose(f) != 0)
            ok = 0;
    } else {
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
    }
    if (!ok) {
        remove(temporary);
        free(temporary);
        return NULL;
    }
    return temporary;
}

int file_store_replace(const char *path, const void *bytes, size_t size)
{
    char *temporary = written_beside(path, bytes, size);
    if (!temporary)
        return -1;
#ifdef _WIN32
    int ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    int ok = rename(temporary, path) == 0;
#endif
    if (!ok)
        remove(temporary);
    free(temporary);
    return ok ? 0 : -1;
}

int file_store_create(const char *path, const void *bytes, size_t size)
{
    char *temporary = written_beside(path, bytes, size);
    if (!temporary)
        return -1;
#ifdef _WIN32
    int ok = MoveFileExA(temporary, path, MOVEFILE_WRITE_THROUGH) != 0;
    int there = !ok && (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS);
#else
    int ok = link(temporary, path) == 0;   /* fails, rather than replaces, where the file is */
    int there = !ok && errno == EEXIST;
#endif
    remove(temporary);
    free(temporary);
    return ok ? 0 : there ? 1 : -1;
}
