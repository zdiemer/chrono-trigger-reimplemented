"""Rescue Queen Leene from Yakra in the Cathedral, 600 A.D."""
from ..campaign import Run, route_arguments, run_route
from . import _trial_cathedral as cathedral

SAVE = "saves/fantasyanime/ctsave01-kidnapping.srm"
HEADER = """# Rescue Queen Leene: the Cathedral, Frog joins, the organs and switches, Yakra defeated.
# Save: ctsave01 slot 2 (saves/fantasyanime/ctsave01-kidnapping.srm).
# Route: yakra
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              until=args.until)
    try:
        # The Cathedral is _trial_cathedral's, which the playthrough and
        # cronos_trial play too.
        cathedral.cathedral(run, args.seed)
        run.finish("Yakra defeated, Queen Leene rescued and the field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
