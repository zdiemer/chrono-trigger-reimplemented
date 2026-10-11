"""Ride a dactyl on the 65,000,000 B.C. world map: board, fly a short way, land and get off.

Save: ctsave07 slot 3 (fantasyanime archive; the party stands on the world map, $1F3, beside a
dactyl at Tyrano Lair, already past the story point that makes dactyls rideable -- ctsave07 slot 2,
nearer the Dactyl's Nest sign, is from before that point: its dactyl's state has neither the "it is
on this map" nor "the party has it out" bit set, and the nearby door leads to a staged fight (the
tribe's hunting ground guarding the nest) whose last stage's touch trigger no walk from the door
reaches, so no movie can ride a dactyl from there without first porting and completing that fight).
"""
from .. import addresses as A
from .. import bfs
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Ride a dactyl on the 65,000,000 B.C. world map: load the save (standing beside a dactyl at Tyrano
# Lair), board it, fly a short way, then land and get off, back on foot. Reaches the dactyl's
# flight, $C2:4CDA-$C2:5627, still the original's (game/overworld/overworld.h).
# Save: ctsave07 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave07-azala.srm"
SLOT = 2
PHASES = ["loaded", "boarded", "flew", "landed"]

TRAVEL_ON_FOOT = 0
TRAVEL_ON_A_DACTYL = 3


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
        # Walk to the dactyl standing beside the party and press confirm next to it: the
        # vehicles' controller (overworld_control_the_vehicles, $C2:309E) boards it once the whole
        # party is aboard, turning overworld_travel from "on foot" to "on a dactyl" (3). ow_bfs's
        # confirm mode presses the button at every tile it tries, since boarding -- like a world
        # map's exits -- only happens by standing next to the vehicle and confirming, not by
        # walking onto it.
        def pos(m):
            return (m.w(A.WORLD_X), m.w(A.WORLD_Y))

        def map_id(m):
            return m.peek(A.MAP_ID, 2).hex()

        def boarded(m, px, py):
            return m.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT

        moves, visited = bfs.ow_bfs(machine, boarded, pos, map_id, confirm=True, max_nodes=800)
        machine.run(60)
        run.say("board", "steps", moves and len(moves), "visited", len(visited),
                "travel", machine.b(A.OVERWORLD_TRAVEL), "frame", machine.frame)
        assert moves is not None, "a dactyl to board"
        assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL, "boarded the dactyl, not the Epoch"
        run.checkpoint("boarded")

    if start <= 2:
        # Fly a short way: the dactyl's flight reads the pad the same frame it reads confirm
        # ($C2:4CDA's dispatch, state 3), so a plain directional hold moves it.
        machine.run(16, "LEFT")
        machine.run(4)
        run.say("flew", "pos", machine.w(A.WORLD_X), machine.w(A.WORLD_Y), "frame", machine.frame)
        assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL, "still on the dactyl"
        run.checkpoint("flew")

    if start <= 3:
        # Land: confirm while flying tests the ground under the dactyl's own spot (the original's
        # $C2:4E49, by way of $C6:E74E, the same ground test the Epoch's landing uses) and, where
        # it is open, starts the touchdown animation (OVERWORLD_DACTYL_STATE bit 5) before the
        # vehicles' controller takes the party off and back to walking once all of it has climbed
        # down (overworld_control_the_vehicles' TRAVEL_GETTING_OFF case).
        machine.run(3, "A")
        for _ in range(40):
            machine.run(10)
            if machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_FOOT:
                break
        machine.run(30)
        run.say("landed", "travel", machine.b(A.OVERWORLD_TRAVEL),
                "dactyl_state", hex(machine.b(A.OVERWORLD_DACTYL_STATE)), "frame", machine.frame)
        assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_FOOT, "back on foot"
        run.checkpoint("landed")

    run.say("end frame", machine.frame)
    run.finish("a dactyl ride on the 65,000,000 B.C. world map")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
