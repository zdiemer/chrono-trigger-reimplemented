#include "storage.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const struct { const char *name, *base, *dir, *ext, *want; } cases[] = {
        { "roms/game.sfc", "roms/game.sfc", NULL, ".srm", "roms/game.srm" },
        { "movies/retry.ctm", "roms/game.sfc", NULL, ".state", "roms/retry.state" },
        { "dotted.dir/game", "dotted.dir/game", NULL, ".assets", "dotted.dir/game.assets" },
        { "game.sfc", "game.sfc", "saves", ".srm", "saves/game.srm" },
        { "roms\\game.sfc", "roms\\game.sfc", NULL, ".srm", "roms\\game.srm" },
        { ".hidden", ".hidden", NULL, ".state", ".hidden.state" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char path[128];
        files_side_path(path, sizeof path, cases[i].name, cases[i].base, cases[i].dir, cases[i].ext);
        if (strcmp(path, cases[i].want)) { fprintf(stderr, "unexpected name: %s\n", path); return 1; }
    }
    puts("player_files_test: OK");
    return 0;
}
