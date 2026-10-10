"""Start a New Game+ from the load screen's fourth row.

Save: technoplaza ct-state16 (a finished cartridge, so the fourth row offers
New Game+ as well as a new game).
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Start a New Game+ from the load screen: load the title, move the cursor down to the fourth row and
# right onto its second choice (New Game+), confirm, carry on from the first slot, accept the default
# Active battle mode and Crono's own name, then a few steps on the 1000 A.D. world map where it starts.
# Save: technoplaza ct-state16 (games.technoplaza.net/snes/chronotrigger/savestates.php, a finished cartridge).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/technoplaza/ct-state16.srm"
PHASES = ["loaded", "started"]


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
        run.say("load screen, frame", machine.frame)
        run.checkpoint("loaded")
    if start <= 1:
        machine.run(3, "DOWN")
        machine.run(20)                        # the cursor lands on the fourth row
        machine.run(3, "RIGHT")
        machine.run(20)                        # its second choice, New Game+
        machine.run(3, "A")
        machine.run(100)                       # which game to carry on from
        machine.run(3, "A")
        machine.run(100)                       # the first slot; the Battle Mode question
        machine.run(3, "A")
        machine.run(60)                        # Active (the default) accepted; Crono's naming screen
        machine.run(3, "START")
        machine.run(30)                        # the naming screen wakes for a button before the cursor moves
        machine.run(3, "START")
        machine.run(60)                        # Start accepts the name offered (Crono's own)
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("New Game+ started, map", run.map_id(), "frame", machine.frame)
        assert run.map_id() not in ("0000", "b101"), "left the load and naming screens for the world map"
        run.checkpoint("started")
    run.say("end frame", machine.frame)
    run.finish("a New Game+ start")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
