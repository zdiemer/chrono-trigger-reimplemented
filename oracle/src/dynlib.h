/* Minimal shared-library loading shim (LoadLibrary / dlopen). */
#ifndef CT_DYNLIB_H
#define CT_DYNLIB_H

typedef struct ct_dynlib ct_dynlib;

ct_dynlib *ct_dynlib_open(const char *path);
void *ct_dynlib_sym(ct_dynlib *lib, const char *name);
void ct_dynlib_close(ct_dynlib *lib);
/* Description of the last failure; valid until the next call. */
const char *ct_dynlib_error(void);

#endif
