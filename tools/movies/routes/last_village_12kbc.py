"""Out of the Earthbound Village onto the frozen 12000 B.C. world map, with the Epoch beside it.

Save: ctsave11 slot 2 (fantasyanime archive; Crono gone, the party in the Earthbound Village,
$1A9, after the Blackbird gave the Epoch its wings).

The way: out of the village onto the world map after Zeal's fall ($1F6), where the winged
Epoch stands parked beside the village (its world-map object, `$C2:42DD`, in a state no
other movie's map shows it in); into the nearest location, and back out onto the map.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# 12000 B.C. after the fall: out of the Earthbound Village onto the frozen world map ($1F6), the winged Epoch parked
# beside it; into the nearest location and back out onto the map (route last_village_12kbc).
# Save: ctsave11 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave11-crono_gone.srm"
SLOT = 1
PHASES = ["loaded", "map", "entered", "left"]
WORLD = "f601"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
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
        assert run.wait_control(), "the party in hand"
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.checkpoint("loaded")
    if start <= 1:
        assert run.exit_to(WORLD), "the world map"
        machine.run(60)
        run.checkpoint("map")
    if start <= 2:
        assert run.world_exit(frozenset({"a901"})), "another location"
        run.wait_control(3000)
        run.checkpoint("entered")
    if start <= 3:
        assert run.exit_to(WORLD), "back onto the world map"
        machine.run(60)
        run.checkpoint("left")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("the 12000 B.C. world map after the fall")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
