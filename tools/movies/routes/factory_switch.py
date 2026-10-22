"""Into the 2300 A.D. factory's lab side and onto its two-across, three-down floor switch.

Save: ctsave03 slot 2 (fantasyanime archive; Crono, Robo and Lucca on the 2300 A.D. world map,
$1F2, beside the factory). The factory's entrance hall ($0E4) has a console that unlocks its two
doors: with Robo leading he overrides the security, otherwise the security's robots attack first
(its script's object $08). The left door leads to the lab side ($0E5), whose floor switch at tiles
(25, 15)-(26, 15) is a two by three, a shape no other movie presses (docs/movies-wanted.md, "Floor
switches"). The party's order is changed on the way in, from the main menu's exchange.
"""
from .. import bfs, policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# The factory's floor switch, 2300 A.D.: load the save (on the world map beside the factory), go in, change the active
# party's order from the main menu, check the entrance hall's console (its security robots attack), win the fight, take
# the left door to the lab side and step on its two-by-three floor switch (route factory_switch).
# Save: ctsave03 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave03-future_more.srm"
SLOT = 1
WORLD_MAP, HALL, LAB_SIDE = "f201", "e400", "e500"
PHASES = ["loaded", "hall", "reordered", "console", "won", "lab", "pressed"]

PARTY = 0x7E2980
MENU_SCREEN = 0x7E0067          # menu_screen: 5 the main menu's exchange
CONSOLE_SPOT = (152, 324)       # below the console (object $08, at (152, 304)), facing up
LEFT_DOOR_CELL = (12, 17)       # the 6-pixel cell before the left door (object $09, tile (4, 5))
LAYER_MAP = 0x7E3000
SWITCH = ((25, 15), (26, 15))   # the switch's bottom tiles


def press(machine, button, gap=30, hold=3):
    machine.run(hold, button)
    machine.run(gap)


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
        assert run.map_id() == WORLD_MAP, "the 2300 A.D. world map"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(max_nodes=800) and run.map_id() == HALL, "into the factory's hall"
        assert run.wait_control(), "the hall in hand"
        run.checkpoint("hall")
    if start <= 2:
        # The main menu's exchange (its fifth option) reorders the active three: the second and
        # third change places.
        before = list(machine.peek(PARTY, 3))
        press(machine, "X", 60)
        for _ in range(4):
            press(machine, "RIGHT", 25)
        press(machine, "A", 60)
        assert machine.b(MENU_SCREEN) == 5, "the exchange open"
        press(machine, "A")
        press(machine, "DOWN")
        press(machine, "A", 60)
        after = list(machine.peek(PARTY, 3))
        run.say("party", before, "->", after, "frame", machine.frame)
        assert sorted(after) == sorted(before) and after != before, "two of the party changed places"
        for _ in range(4):
            press(machine, "B", 40)
        assert run.wait_control(), "the hall in hand again"
        run.checkpoint("reordered")
    if start <= 3:
        x, y = CONSOLE_SPOT
        assert run.walk(lambda m, px, py: abs(px - x) <= 4 and abs(py - y) <= 6) is not None, "below the console"
        machine.run(2, "UP")
        machine.run(8)
        machine.run(3, "A")
        for _ in range(40):
            machine.run(30)
            if machine.b(0x7E95D5) < 0x80:
                break
            if run.dialog():
                machine.run(3, "A")
        run.checkpoint("console")
    if start <= 4:
        seen = []

        def won(r, state):
            living = policies.enemies(state)
            if living:
                seen.append(True)
            return bool(seen) and not living and any(state.hp(slot) for slot in range(3))

        run.fight("party", won, party=3, seed=args.seed)
        assert run.wait_control(), "the hall in hand after the fight"
        run.checkpoint("won")
    if start <= 5:
        cx, cy = LEFT_DOOR_CELL
        moves, _ = bfs.bfs_walk(machine, lambda m, px, py: int(px) // 6 == cx and int(py) // 6 == cy
                                and m.peek(0x7E0100, 2).hex() == HALL, run._pos(), run._map(), cell=6,
                                max_nodes=6000)
        assert moves is not None, "before the left door"
        for _ in range(2):
            machine.run(2, "UP")
            machine.run(8)
            machine.run(3, "A")
            machine.run(60)
        machine.run(20, "UP")
        machine.run(120)
        assert run.map_id() == LAB_SIDE, "the lab side"
        assert run.wait_control(), "the lab side in hand"
        run.checkpoint("lab")
    if start <= 6:
        before = [tile_at(machine, c, r) for c, r in SWITCH]
        moves = run.walk(lambda m, px, py: m.peek(0x7E0100, 2).hex() == LAB_SIDE
                         and [m.b(LAYER_MAP + r * 0x100 + c) for c, r in SWITCH] != before, max_nodes=6000)
        machine.run(40)
        run.say("switch", [hex(t) for t in before], "->", [hex(tile_at(machine, c, r)) for c, r in SWITCH],
                "me", run.me(), "frame", machine.frame)
        assert moves is not None, "the switch pressed"
        assert run.wait_control(), "the lab side in hand"
        run.checkpoint("pressed")
    run.finish("the factory's floor switch")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
