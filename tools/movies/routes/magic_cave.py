"""At the Magic Cave's mountainside, Frog remembers Glenn and Cyrus, 600 A.D.

Save: ctsave06 slot 1 (fantasyanime archive; the party on the 600 A.D. world map beside the
mountain, $1F1).

The way: step from the world map into the mountainside ($0A3), where the scene plays: the
party walks up to the rock, and Frog, holding the Masamune, remembers -- the forest ($077)
and then Glenn and Cyrus on the bridge ($087), the flashback's first lines. The scene's walks
are the event script's moves to a tile (`$C0:4F61`, opcodes $96/$97/$9A/$A0/$A1), which no
other movie meets in a state like this one. The route stops a few lines into the flashback,
long before the party is in hand again, to keep the movie short.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# 600 A.D.: from the world map into the mountainside ($0A3), where the party walks up to the rock and Frog, holding
# the Masamune, remembers: the forest ($077), then Glenn and Cyrus on the bridge ($087), a few lines (route magic_cave).
# Save: ctsave06 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave06-magus_castle.srm"
SLOT = 0
PHASES = ["loaded", "mountain", "forest", "bridge"]
MOUNTAIN, FOREST, BRIDGE = "a300", "7700", "8700"


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
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(), "a location on the world map"
        assert run.map_id() == MOUNTAIN, "the mountainside"
        run.checkpoint("mountain")
    if start <= 2:
        assert run.dialogs_until(lambda: run.map_id() == FOREST, 4000), "the forest of Frog's memory"
        run.checkpoint("forest")
    if start <= 3:
        assert run.dialogs_until(lambda: run.map_id() == BRIDGE, 6000), "Glenn and Cyrus on the bridge"
        assert run.dialogs_until(run.dialog, 2000), "the flashback's first line"
        run.close_dialogs(1200)
        run.checkpoint("bridge")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("Frog's flashback")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
