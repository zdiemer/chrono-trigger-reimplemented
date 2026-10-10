"""Walk one of the 12,000 B.C. world maps ($1F4), into a location and back out.

Save: ctsave08 slot 2 (fantasyanime archive; the party stands on this world map).
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Walk the 12,000 B.C. world map at location $1F4: load the save (standing on the world map), walk
# until a location's door is found and step through it, then walk back out onto the world map.
# Save: ctsave08 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave08-zeal.srm"
SLOT = 1
PHASES = ["loaded", "entered", "left"]


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              recomp=args.recomp, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        run.select_slot(SLOT)
        machine.run(4, "A")
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(), "a location on the world map"
        run.checkpoint("entered")
    if start <= 2:
        run.wait_control()
        assert run.exit_other(frozenset()), "back onto the world map"
        run.checkpoint("left")
    run.say("end frame", machine.frame)
    run.finish("the 12,000 B.C. world map at $1F4")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
