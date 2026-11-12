#include "bindings.h"
#include "storage.h"
#include <stdlib.h>

int bindings_configure(const char *keys_path, const char *const *binds, int nbinds)
{
    /* The window's controls: the defaults, the key file, then --bind. */
    bindings_reset();
    char default_keys[1024] = "";
    if (!keys_path) {
        char *base = SDL_GetBasePath();
        if (base) {
            snprintf(default_keys, sizeof default_keys, "%s%s", base, "ct_play.keys");
            SDL_free(base);
        }
    }
    const char *keys_file = keys_path ? keys_path : default_keys[0] ? default_keys : NULL;
    if (keys_file) {
        size_t len = 0;
        bool missing;
        char *text = (char *)files_read(keys_file, &len, true, &missing);
        if (!text && (keys_path || !missing)) {
            fprintf(stderr, "cannot read %s\n", keys_file);
            return 2;
        }
        if (text && bindings_load(text, len, keys_file) != 0) {
            free(text);
            return 2;
        }
        free(text);
    }
    for (int i = 0; i < nbinds; i++) {
        char err[160];
        if (bindings_set(binds[i], err, sizeof err) != 0) {
            fprintf(stderr, "bad --bind %s: %s\n", binds[i], err);
            return 2;
        }
    }
    return 0;
}
