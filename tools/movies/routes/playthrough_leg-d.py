"""Leg D of the playthrough: from the Blackbird, after Crono is gone, to an ending.

Save: ctsave11 slot 1 (fantasyanime archive; the party held in the Blackbird).

The main playthrough (playthrough.py) plays the early game from a blank
cartridge; the legs start from cartridge saves so the whole game is played in
parallel. This one plays the end of the game with the save's party and
items: the Blackbird's equipment recovery and Dalton (blackbird_save,
dalton), ... Each phase ends at a checkpoint and the route goes on only past
checkpoints that succeeded; where another route already plays a stretch, its
`play(run, start)` plays it from the state this route reached.
"""
from ..campaign import Run, route_arguments, run_route
from . import blackbird_save
from . import dalton

SAVE = "saves/fantasyanime/ctsave11-crono_gone.srm"
BLACKBIRD = blackbird_save.PHASES
DALTON = dalton.PHASES[1:]
PHASES = BLACKBIRD + DALTON
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# Leg D of the playthrough, from the Blackbird to an ending (route playthrough_leg-d).
# Save: ctsave11 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start < len(BLACKBIRD):
            blackbird_save.play(run, start)
        later = start - len(BLACKBIRD)
        if later < len(DALTON):
            dalton.play(run, max(1, later + 1), args.seed)
        later -= len(DALTON)
        run.finish("leg D's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    except (AssertionError, RuntimeError):
        machine.save_state(args.prefix + "_failed.st")
        machine.screenshot(args.prefix + "_failed.png")
        run.say("failed at", machine.frame, "map", run.map_id())
        raise
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
