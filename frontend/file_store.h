/* Desktop persistence: replace a named binary file only after its new bytes
 * have been written and closed successfully. The temporary file is beside
 * the destination so the replacement stays on one filesystem. */
#ifndef CT_FRONTEND_FILE_STORE_H
#define CT_FRONTEND_FILE_STORE_H

#include <stddef.h>

int file_store_replace(const char *path, const void *bytes, size_t size);
/* Write a named file only where there is none, as a whole: 0 written, 1 when
 * the file was there first (another process may have just written it), -1
 * when it couldn't be written. */
int file_store_create(const char *path, const void *bytes, size_t size);

#endif
