"""Fly the winged Epoch into Lavos over 1999 A.D.: the scene program's mode 6, the Epoch breaking through.

Save: ctsave14 slot 1 (fantasyanime archive; the party stands on the 1000 A.D. world map beside the
Epoch, its wings on: $7F:00BA bit 7).

Board the Epoch, open the time dial and turn it from 1000 A.D. up to 1999 A.D.: travelling there
in the winged Epoch goes straight to location $1B0, the party in the Epoch over Lavos, where Lucca
asks "Are we REALLY going on?". Fight! (with the wings, $1B0's object $0B: "We must use the Epoch
to break through!") sets the location to Lavos's ($1DA) and hands the screen to the scene
program at mode 6 (event $FF $26, $7E:78DD): the Epoch diving into Lavos, which no other movie
plays. The route stops when the field at $1DA opens its first window, the scene over.
"""
from .. import addresses as A
from .. import bfs
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# The winged Epoch flown into Lavos: load the save on the 1000 A.D. world map, board the Epoch, turn the time dial up to
# 1999 A.D. and travel; over Lavos, answer Lucca's "Are we REALLY going on?" with Fight!, and the scene program's mode 6
# (event $FF $26) plays the Epoch breaking through, until the first window of Lavos's field (route epoch_lavos).
# Save: ctsave14 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
SLOT = 0
PHASES = ["loaded", "boarded", "dialed", "asked", "breaking", "through"]

WORLD_1000, OVER_LAVOS, LAVOS = "f001", "b001", "da01"
TRAVEL_IN_THE_EPOCH = 2
DIAL_PERIOD = 0x7E0C52          # the period the time dial stands at: 3 1000 A.D., 2 1999 A.D. (one turn up)
PERIOD_1999 = 2
SCENE_MODE = 0x7E0384           # the scene program's mode, as $C3:0000 stores the event's operand
BREAK_THROUGH = 0x26            # event $FF $26: mode 6


def wait_until(machine, condition, frames, step=10):
    """Run without pressing anything until `condition()`, for at most `frames`."""
    for _ in range(frames // step):
        if condition():
            return True
        machine.run(step)
    return condition()


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
        assert run.map_id() == WORLD_1000, "the 1000 A.D. world map"
        run.checkpoint("loaded")
    if start <= 1:
        def pos(m):
            return (m.w(A.WORLD_X), m.w(A.WORLD_Y))

        def map_id(m):
            return m.peek(A.MAP_ID, 2).hex()

        moves, _ = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != 0, pos, map_id,
                              confirm=True, max_nodes=800)
        assert moves is not None, "the Epoch to board"
        for _ in range(60):
            machine.run(10)
            if machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH:
                break
        assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH, "aboard the Epoch"
        machine.run(30)
        run.checkpoint("boarded")
    if start <= 2:
        machine.run(4, "Y")                         # the time dial
        machine.run(100)
        for _ in range(6):
            if machine.b(DIAL_PERIOD) == PERIOD_1999:
                break
            machine.run(4, "UP")
            machine.run(56)
        assert machine.b(DIAL_PERIOD) == PERIOD_1999, "the dial at 1999 A.D."
        run.checkpoint("dialed")
    if start <= 3:
        machine.run(4, "A")                         # travel
        assert wait_until(machine, lambda: run.map_id() == OVER_LAVOS and run.dialog(), 3000), \
            "over Lavos, Lucca's question"
        machine.run(120)                            # the question typed out
        run.checkpoint("asked")
    if start <= 4:
        machine.run(3, "A")                         # Fight!, the first choice
        machine.run(40)
        assert run.dialogs_until(lambda: machine.b(SCENE_MODE) == BREAK_THROUGH, 6000), "the Epoch's dive begins"
        run.say("scene mode", hex(machine.b(SCENE_MODE)), "map", run.map_id(), "frame", machine.frame)
        run.checkpoint("breaking")
    if start <= 5:
        # Stopped at the field's first window rather than where the field starts loading over the
        # program: a movie ending inside the field's entry leaves shadow mode's comparison of it open.
        assert wait_until(machine, lambda: run.map_id() == LAVOS and run.dialog(), 6000), \
            "Lavos's field and its first window"
        run.checkpoint("through")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("the winged Epoch through Lavos's shell")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
