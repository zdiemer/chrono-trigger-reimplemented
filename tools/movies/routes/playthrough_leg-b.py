"""Leg B of the playthrough: 65,000,000 B.C. to Tyrano Lair.

Save: ctsave05 slot 1 (fantasyanime archive; Crono, Ayla and Marle on the 65,000,000 B.C.
world map before the Forest Maze, story $78).

One of the branched playthrough's legs: the main playthrough (route playthrough) plays the
early game from a blank cartridge, and each leg starts from a cartridge save so that the whole
game is played in parallel. This one goes through the Forest Maze to the Reptite Lair and
Nizbel (its piece, route nizbel), and on. The party and items are the save's. Each phase ends
at a checkpoint, and the route goes on only past checkpoints that succeeded.
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import world_enter
from . import _early_bosses as early
from . import nizbel
from . import playthrough as main_leg

SAVE = "saves/fantasyanime/ctsave05-prehistoric.srm"
SLOT = 0
HEADER = """\
# Leg B of the playthrough (route playthrough_leg-b): 65,000,000 B.C. to Tyrano Lair.
# Save: ctsave05 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

WORLD_65M = "f301"
FOREST_MAZE = 0x11A
FOREST_MAZE_MAP = "1a01"
STORY_BEFORE_MAZE = 0x78
# The maze's way out at its foot (tile 6-8, 47), onto the world map by the Reptite Lair.
MAZE_FOOT = (7 * 16 + 8, 47 * 16 + 8)
MAZE_SEARCH = 8000          # places the walk may try: the maze's dead ends lie between its head and foot
BELOW_MAZE = 740            # world-map pixel row: below the maze, by the lair (above it, back by its head)

START = ["loaded", "settings", "forest_maze", "maze_out"]
NIZBEL = nizbel.PHASES[2:]
PHASES = START + NIZBEL
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, WORLD_65M, STORY_BEFORE_MAZE)
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        if start <= 2:
            run.wait_world_control()
            world_enter(run, FOREST_MAZE)
            assert main_leg.in_hand(run, 20000)
            run.say("maze", run.map_id(), run.me(), "story", hex(machine.b(A.EVENT_MEMORY)))
            run.checkpoint("forest_maze")
        if start <= 3:
            main_leg.through(run, *MAZE_FOOT, lambda: run.map_id() == WORLD_65M and run.world_pos()[1] > BELOW_MAZE,
                             rounds=30, limit=MAZE_SEARCH)
            run.say("out", run.world_pos(), "story", hex(machine.b(A.EVENT_MEMORY)))
            run.checkpoint("maze_out")
        if start < len(START) + len(NIZBEL):
            nizbel.play(run, max(2, start - len(START) + 2), args.seed)
        run.finish("the leg's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
