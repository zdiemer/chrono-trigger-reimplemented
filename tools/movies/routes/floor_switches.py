"""Step on the floor switch in Lucca's house from its left column, then open and close the menus,
which presses it again from what the map remembers.

Save: ctsave14 slot 1 (fantasyanime archive; the party of seven on the 1000 A.D. world map, $1F0,
a step from Lucca's house, $004). A floor switch is a set of BG1 map tiles whose bottom row has the
step event: stepping onto one of those bottom tiles presses it, which moves each of its tiles on to
the next tile in the tile set (game/field/switches.c), and which column the step is in picks one of
the press's paths. The only switch the library presses is a two-by-two from its right column, in
the telepod ending. The house's map has a two-by-two at tiles (7, 10)-(8, 10), which the search
steps onto from its left column, and two more switches at (2, 4)-(3, 4) and (10, 4)-(11, 4) that no
walk in the house reaches. As the menus close, the map's screen is set up afresh and the switches
pressed on it pressed again (docs/movies-wanted.md, "Floor switches").
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# A floor switch in Lucca's house: load the save (on the 1000 A.D. world map beside the house), go in, step on the
# two-by-two floor switch there from its left column, then open the menus and close them, the switch pressed again from
# what the map remembers (route floor_switches).
# Save: ctsave14 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
SLOT = 0
LUCCAS_HOUSE = "0400"
PHASES = ["loaded", "inside", "pressed", "back"]

LAYER_MAP = 0x7E3000            # field_layer_maps: BG1's tile a byte, a row every $100
TILE = 16                       # a map tile's pixels

# The switch: the column stepped on, its bottom row, the other column; and that tile unpressed.
SWITCH = (7, 10, 8)
SWITCH_TILE = 0xEC


def tile_at(machine, column, row):
    return machine.b(LAYER_MAP + row * 0x100 + column)


def press(run, column, row, other):
    """Walk until the leader stands in the switch's column `column` and its tiles have moved on."""
    machine = run.machine
    before = tile_at(machine, column, row), tile_at(machine, other, row)

    def pressed(m, px, py):
        return (m.peek(A.MAP_ID, 2).hex() == LUCCAS_HOUSE and int(px) // TILE == column
                and (m.b(LAYER_MAP + row * 0x100 + column), m.b(LAYER_MAP + row * 0x100 + other)) != before)

    moves = run.walk(pressed, max_nodes=6000)
    machine.run(40)
    run.say("switch at", (column, row), "steps", moves and len(moves), "me", run.me(),
            "tiles", [hex(t) for t in before], "->", [hex(tile_at(machine, column, row)), hex(tile_at(machine, other, row))],
            "frame", machine.frame)
    assert moves is not None, "the switch at %d, %d pressed" % (column, row)


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
        assert run.map_id() == "f001", "the 1000 A.D. world map"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(max_nodes=400) and run.map_id() == LUCCAS_HOUSE, "into Lucca's house"
        assert run.wait_control(), "the house in hand"
        run.checkpoint("inside")
    if start <= 2:
        press(run, *SWITCH)
        run.checkpoint("pressed")
    if start <= 3:
        # The menus opened and closed: the map's screen is set up afresh, its tiles unpacked again,
        # and the switches pressed on it are pressed again from the steps it remembers
        # (field_press_the_remembered_switches, $C0:28E1).
        assert run.wait_control(), "the house in hand"
        machine.run(3, "X")
        machine.run(90)
        machine.run(3, "B")
        machine.run(90)
        assert run.wait_control(), "the house in hand again"
        column, row, other = SWITCH
        assert tile_at(machine, column, row) != SWITCH_TILE, "the switch still pressed"
        run.checkpoint("back")
    assert run.wait_control(), "the field in hand"
    run.finish("the floor switch in Lucca's house")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
