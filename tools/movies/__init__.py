"""Authoring input movies from game state (docs/MOVIES.md).

The modules here drive `ct_drive` (dev/ct_drive.c), the development system as a
scriptable server, and decide what to press by reading the machine's memory
rather than by counting frames:

    addresses.py   the game memory this code reads, and how each address was found
    ctdrv.py       the client: run frames, read memory, checkpoint, log input
    mkmovie.py     buttons and the .ctm format, and a CLI that writes one
    bfs.py         breadth-first walking searches over checkpoints (field and world map)
    bot.py         battle menus: what is on screen, and how to reach a command
    policies.py    what a character should do, given the battle state
    campaign.py    the route runner: phases, checkpoints, dialog, fights with backtracking
    routes/        the routes used for the ending movies in oracle/movies/saves
    check_drive.py ct_drive against ct_dev --native off (needs the ROM)
    route_check.py routes as a test of a native-code mode: each run's outcome, modes side by side

Everything in here is stdlib-only and imports without a ROM; running a route
needs the ROM, a build of ct_drive and (for the save routes) a cartridge save.
"""
