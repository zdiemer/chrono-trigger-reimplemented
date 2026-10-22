"""A longer dactyl flight on the 65,000,000 B.C. world map: the turns, a landing refused, the menus
opened in the air, then down.

Save: ctsave07 slot 3 (fantasyanime archive; the party stands on the world map, $1F3, beside a
dactyl at Tyrano Lair), as dactyl_65m_bc. That route boards, flies a few tiles and lands; this one
flies the eight ways in turn, so the two following dactyls swing round behind the leader's into
the V the other way, presses confirm over ground no dactyl comes down on, opens the menus in
flight and presses the map view's button (which opens nothing on this map), then searches for
ground to land on (docs/movies-wanted.md, "World maps"). The map comes back from the menus with
the dactyls where they were, not set down afresh in the air, so that path of their setting down
($C2:4D19, $C2:50F0, $C2:54B5) stays unreached.
"""
from .. import addresses as A
from .. import bfs
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# A longer dactyl flight on the 65,000,000 B.C. world map: load the save (standing beside a dactyl at Tyrano Lair), board
# it, fly the eight ways in turn, press confirm over ground no dactyl lands on, open the menus in the air and close them,
# press the map view's button, then land and get off (route dactyl_flight_65m_bc).
# Save: ctsave07 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave07-azala.srm"
SLOT = 2
PHASES = ["loaded", "boarded", "flew", "refused", "menus", "map_view", "landed"]

TRAVEL_ON_FOOT = 0
TRAVEL_ON_A_DACTYL = 3
COMING_DOWN = 0x20              # OVERWORLD_DACTYL_STATE: the landing confirm started
TRY_SLOT = 4080                 # the machine copy a landing is tried from

WAYS = ["LEFT", "UP+LEFT", "UP", "UP+RIGHT", "RIGHT", "DOWN+RIGHT", "DOWN", "DOWN+LEFT"]


def world_pos(machine):
    return (machine.w(A.WORLD_X), machine.w(A.WORLD_Y))


def world_map(machine):
    return machine.peek(A.MAP_ID, 2).hex()


def flying(machine):
    return machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL and not machine.b(A.OVERWORLD_DACTYL_STATE) & COMING_DOWN


def lands_here(machine):
    """Press confirm and see whether the dactyl starts down; the machine is put back either way."""
    machine.mark(TRY_SLOT)
    machine.run(3, "A")
    machine.run(12)
    down = bool(machine.b(A.OVERWORLD_DACTYL_STATE) & COMING_DOWN)
    machine.restore(TRY_SLOT)
    machine.free(TRY_SLOT)
    return down


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
        # Boarding as dactyl_65m_bc does: confirm beside it at every tile tried.
        moves, visited = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT,
                                    world_pos, world_map, confirm=True, max_nodes=800)
        machine.run(90)
        assert moves is not None and machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL, "boarded the dactyl"
        run.checkpoint("boarded")

    if start <= 2:
        # Each way held long enough for the followers to swing round into the V behind it.
        for way in WAYS:
            machine.run(40, way)
            machine.run(10)
            run.say("flew", way, "at", run.world_pos(), "frame", machine.frame)
            assert flying(machine), "still in the air"
        run.checkpoint("flew")

    if start <= 3:
        # Confirm where it may not come down: fly on until a spot refuses it (water, the highest
        # walls, the Epoch's spot), then press there.
        for step in range(60):
            if not lands_here(machine):
                break
            machine.run(8, WAYS[(step // 8) % len(WAYS)])
        else:
            raise AssertionError("a spot the dactyl won't land on")
        machine.run(3, "A")
        machine.run(40)
        run.say("refused at", run.world_pos(), "frame", machine.frame)
        assert flying(machine), "the landing refused"
        run.checkpoint("refused")

    if start <= 4:
        machine.run(3, "X")
        machine.run(120)
        for _ in range(2):
            machine.run(3, "B")
            machine.run(40)
        machine.run(120)
        machine.run(20, "RIGHT")
        run.say("menus closed, at", run.world_pos(), "frame", machine.frame)
        assert flying(machine), "back in the air after the menus"
        run.checkpoint("menus")

    if start <= 5:
        machine.run(3, "Y")
        machine.run(120)
        machine.run(20, "UP")
        machine.run(3, "B")
        machine.run(150)
        machine.run(20, "LEFT")
        run.say("map view closed, at", run.world_pos(), "frame", machine.frame)
        assert flying(machine), "back in the air after the map view"
        run.checkpoint("map_view")

    if start <= 6:
        # Down where it may land: the overworld search, pressing confirm at every tile it tries.
        moves, visited = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_DACTYL_STATE) & COMING_DOWN,
                                    world_pos, world_map, confirm=True, max_nodes=800)
        assert moves is not None, "somewhere to land"
        for _ in range(40):
            machine.run(10)
            if machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_FOOT:
                break
        machine.run(30)
        run.say("landed at", run.world_pos(), "frame", machine.frame)
        assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_FOOT, "back on foot"
        run.checkpoint("landed")

    run.finish("a long dactyl flight on the 65,000,000 B.C. world map")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
