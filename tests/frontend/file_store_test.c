/* Fault injection at the I/O boundary: a failed or interrupted replacement
 * must never truncate the previous cartridge save. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fault;
static size_t write_with_fault(const void *data, size_t size, size_t count, FILE *f);
static int close_with_fault(FILE *f);
#define fwrite write_with_fault
#define fclose close_with_fault
#include "../../frontend/file_store.c"
#undef fwrite
#undef fclose

static size_t write_with_fault(const void *data, size_t size, size_t count, FILE *f)
{
    if (fault == 1 || fault == 3) {
        size_t written = fwrite(data, size, count / 2, f);
        if (fault == 3) {
            fflush(f);
            _Exit(77);  /* process interrupted with only half the temporary file written */
        }
        return written;
    }
    return fwrite(data, size, count, f);
}

static int close_with_fault(FILE *f)
{
    int rc = fclose(f);
    return fault == 2 ? EOF : rc;
}

static int matches(const char *path, const unsigned char *expected, size_t size)
{
    unsigned char bytes[8193];
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t got = fread(bytes, 1, sizeof bytes, f);
    int rc = fclose(f);
    return rc == 0 && got == size && !memcmp(bytes, expected, size);
}

int main(int argc, char **argv)
{
    unsigned char before[8192], after[8192];
    memset(before, 0x55, sizeof before);
    memset(after, 0xAA, sizeof after);
    if (argc == 3 && !strcmp(argv[1], "--interrupt")) {
        fault = 3;
        file_store_replace(argv[2], after, sizeof after);
        return 99;
    }
    if (argc != 2)
        return 2;
    if (file_store_replace(argv[1], before, sizeof before) != 0)
        return 3;
    for (fault = 1; fault <= 2; fault++) {
        if (file_store_replace(argv[1], after, sizeof after) == 0 || !matches(argv[1], before, sizeof before))
            return 4;
    }
    fault = 0;
    if (file_store_replace(argv[1], after, sizeof after) != 0 || !matches(argv[1], after, sizeof after))
        return 5;
    puts("file_store_test: short write, close failure and round trip OK");
    return 0;
}
