"""A two-across, three-down floor switch in the Northern Ruins, stepped on from its left column.

Save: ctsave14 slot 2 (fantasyanime archive; the party on the 600 A.D. world map, $1F1, a step
from the Northern Ruins, $041, which the carpenter has repaired). The ruins' rooms are one map with
several records; the one at $043 has a two by three at tiles (58, 38)-(59, 38), a short walk from
where the party comes in. `factory_switch` presses a two by three from its right column; this one
is pressed from the left, the press's other path. Then the menus are opened and closed, which presses
it again from what the map remembers (docs/movies-wanted.md, "Floor switches").
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# A floor switch in the Northern Ruins, 600 A.D.: load the save (on the world map beside the ruins), go in, walk to the
# room at $043 and step on its two-by-three floor switch from the left column, then open the menus and close them
# (route northern_ruins_switch).
# Save: ctsave14 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
SLOT = 1
WORLD_MAP, RUINS, SWITCH_ROOM = "f101", "4100", "4300"
PHASES = ["loaded", "ruins", "room", "pressed", "menus"]

LAYER_MAP = 0x7E3000
TILE = 16
COLUMN, ROW, OTHER = 58, 38, 59  # the switch's left column, its bottom row, the right column


def tile_at(machine, column, row):
    return machine.b(LAYER_MAP + row * 0x100 + column)


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
        assert run.map_id() == WORLD_MAP, "the 600 A.D. world map"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(max_nodes=400) and run.map_id() == RUINS, "into the Northern Ruins"
        assert run.wait_control(), "the ruins in hand"
        run.checkpoint("ruins")
    if start <= 2:
        assert run.exit_to(SWITCH_ROOM), "the switch's room"
        assert run.wait_control(), "the room in hand"
        run.checkpoint("room")
    if start <= 3:
        before = tile_at(machine, COLUMN, ROW), tile_at(machine, OTHER, ROW)

        def pressed(m, px, py):
            return (m.peek(0x7E0100, 2).hex() == SWITCH_ROOM and int(px) // TILE == COLUMN
                    and (m.b(LAYER_MAP + ROW * 0x100 + COLUMN), m.b(LAYER_MAP + ROW * 0x100 + OTHER)) != before)

        moves = run.walk(pressed, max_nodes=6000)
        machine.run(40)
        run.say("switch", [hex(t) for t in before], "->", [hex(tile_at(machine, COLUMN, ROW)),
                hex(tile_at(machine, OTHER, ROW))], "me", run.me(), "frame", machine.frame)
        assert moves is not None, "the switch pressed from its left column"
        run.checkpoint("pressed")
    if start <= 4:
        assert run.wait_control(), "the room in hand"
        machine.run(3, "X")
        machine.run(90)
        machine.run(3, "B")
        machine.run(90)
        assert run.wait_control(), "the room in hand again"
        assert tile_at(machine, COLUMN, ROW) != 0xFA, "the switch still pressed"
        run.checkpoint("menus")
    run.finish("the Northern Ruins' floor switch")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
