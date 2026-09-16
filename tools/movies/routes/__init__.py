"""Routes: the scripts that produced the ending movies in oracle/movies/saves.

Each one boots a cartridge save, plays to an ending and writes a `.ctm`:

    python3 -m tools.movies.routes.telepod_lavos --sram "<ct-state16>/Chrono Trigger (U).srm" \
        --prefix work/telepod --movie oracle/movies/saves/endings-telepod-lavos.ctm

They decide what to press from memory, so they survive changes to the hardware
model's timing; the movies they write do not (docs/MOVIES.md). The saves are the
ones docs/MOVIES.md names; none of them is in the repo.
"""
