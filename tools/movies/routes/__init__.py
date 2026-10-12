"""Routes: the scripts that produced the ending movies in oracle/movies/saves.

Each one boots a cartridge save, plays to an ending and writes a `.ctm`:

    python3 -m tools.movies.routes.telepod_lavos --prefix work/telepod \
        --movie oracle/movies/saves/endings-telepod-lavos.ctm

They decide what to press from memory, so they survive changes to the hardware
model's timing; the movies they write do not (docs/MOVIES.md). That also makes
them the test of the native-code modes movies can't follow
(`--native on --generated on`; route_check.py). Each names its cartridge save in
`SAVE` (oracle/saves/technoplaza), the default for `--sram`.
"""
