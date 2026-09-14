#include "dynlib.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <windows.h>

static char last_error[128];

ct_dynlib *ct_dynlib_open(const char *path) {
    HMODULE h = LoadLibraryA(path);
    if (!h)
        snprintf(last_error, sizeof last_error, "LoadLibrary failed (error %lu)", GetLastError());
    return (ct_dynlib *)h;
}

void *ct_dynlib_sym(ct_dynlib *lib, const char *name) {
    void *sym = (void *)GetProcAddress((HMODULE)lib, name);
    if (!sym)
        snprintf(last_error, sizeof last_error, "symbol %s not found", name);
    return sym;
}

void ct_dynlib_close(ct_dynlib *lib) {
    if (lib)
        FreeLibrary((HMODULE)lib);
}

const char *ct_dynlib_error(void) { return last_error; }

#else
#include <dlfcn.h>

ct_dynlib *ct_dynlib_open(const char *path) { return (ct_dynlib *)dlopen(path, RTLD_NOW | RTLD_LOCAL); }

void *ct_dynlib_sym(ct_dynlib *lib, const char *name) { return dlsym(lib, name); }

void ct_dynlib_close(ct_dynlib *lib) {
    if (lib)
        dlclose(lib);
}

const char *ct_dynlib_error(void) {
    const char *e = dlerror();
    return e ? e : "unknown error";
}
#endif
