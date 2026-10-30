"""Defeat the R Series in the Factory Ruins, starting from ctsave03 slot 2.

The lab scripts ($0E5, $0E6, $0EB) and the location exit lists establish the
path. The lower console checks X, A, B, Y directly; its condition does not
require the warehouse cranes. Power sets story $3F, and returning up the
lab corridor starts the R Series scene, removing Robo from the active party.
"""
from .. import bfs, policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Factory Ruins, 2300 A.D.: open the lab security doors, enter the lower security code, restore power,
# escape the closing shutters, and defeat the six R Series robots; Robo's rescue, repair and the Gate trip
# follow, ending with the field back in hand at the End of Time.
# Route: r_series. Save: ctsave03 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave03-future_more.srm"
SLOT = 1
WORLD_MAP, HALL, LAB_SIDE = "f201", "e400", "e500"
PHASES = ["loaded", "hall", "reordered", "console", "won", "lab", "upper", "security",
          "basement", "unlocked", "power", "escaped", "battle", "boss_won", "after"]

PARTY = 0x7E2980
MENU_SCREEN = 0x7E0067          # menu_screen: 5 the main menu's exchange
CONSOLE_SPOT = (152, 324)       # below the console (object $08, at (152, 304)), facing up
LEFT_DOOR_CELL = (12, 17)       # the 6-pixel cell before the left door (object $09, tile (4, 5))
CONTROL = 0x7E011F
SECURITY_FLAGS = 0x7F0058
CODE_FLAGS = 0x7F01D0
STORY = 0x7F0000
LOWER_LAB, BASEMENT = "e600", "eb00"


def press(machine, button, gap=30, hold=3):
    machine.run(hold, button)
    machine.run(gap)


def win(run, seed=1, party=3, policy="party_with_lapis"):
    machine = run.machine
    assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "a battle menu"
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(party))

    run.fight(policy, won, party=party, seed=seed)


def approach(run, x, y, seed):
    """Walk to a console, resolving the lab's coordinate-triggered encounters."""
    machine = run.machine
    for _ in range(8):
        moves = run.walk(lambda m, px, py: (abs(px - x) <= 5 and abs(py - y) <= 5)
                         or m.b(CONTROL) == 0)
        assert moves is not None, "the lab console approach"
        if machine.b(CONTROL):
            return
        win(run, seed)
        assert run.wait_control(), "the lab after an encounter"
    raise AssertionError("too many encounters on the console approach")


def check_console(run):
    # The collision stops the leader 23 pixels below these consoles; the
    # usual talk() offset of 18 pixels lies inside their blocking tiles.
    machine = run.machine
    machine.run(20, "UP")
    machine.run(8)
    machine.run(3, "A")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)
    if start > 0:
        run.find_player()

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
        # $0E5 object 12 appears after the three enemies. Its interaction sets
        # $7F:005C bit 5 and opens the ladder at tile (34, 10).
        approach(run, 456, 112, args.seed)
        check_console(run)
        assert run.wait_control(), "the upper lab console"
        assert machine.b(0x7F005C) & 0x20, "the ladder passage open"
        assert run.exit_to(LOWER_LAB), "down the lab ladder"
        assert run.wait_control(), "the lower lab in hand"
        run.checkpoint("upper")
    if start <= 7:
        # $0E6 object 17: Robo opens both lower doors, then five enemies attack.
        approach(run, 360, 744, args.seed)
        check_console(run)
        win(run, args.seed)
        assert run.wait_control(), "the lower security fight won"
        assert machine.b(SECURITY_FLAGS) & 1, "the lower doors open"
        run.checkpoint("security")
    if start <= 8:
        assert run.exit_to(BASEMENT), "down the basement ladder"
        assert run.wait_control(), "the basement in hand"
        run.checkpoint("basement")
    if start <= 9:
        # Walk to the ladder's foot before turning: the corner at y=784 is
        # narrower than a BFS cell, which can otherwise discard the turn.
        for _ in range(80):
            if run.me()[1] >= 783:
                break
            machine.run(4, "DOWN")
        assert run.me()[1] >= 783, "the basement ladder's foot"
        assert run.go(712, 360, 8), "below the code console's interaction object 15"
        check_console(run)
        # Object 14 runs function 3 for object 15. At script offset $0588
        # it has finished its prompt and is testing the first held button.
        for _ in range(200):
            if machine.w(0x7E119C) == 0x0588:
                break
            if run.dialog():
                machine.run(3, "A")
            machine.run(10)
        assert machine.w(0x7E119C) == 0x0588, "the code reader ready"
        for button, variable in (("X", 9), ("A", 10), ("B", 11), ("Y", 12)):
            accepted = 0x7F0200 + 2 * variable
            for _ in range(120):
                machine.run(1, button)
                if machine.b(accepted):
                    break
            assert machine.b(accepted), "code button accepted: " + button
            machine.run(12)
        assert run.wait_control(), "the code console finished"
        assert machine.b(CODE_FLAGS) & 1, "the power room unlocked"
        run.checkpoint("unlocked")
    if start <= 10:
        assert run.go(824, 170, 8), "the power switch, object 10"
        check_console(run)
        assert run.dialogs_until(lambda: machine.b(STORY) == 0x3F, 8000), "power restored"
        assert run.wait_control(), "the power room in hand"
        run.checkpoint("power")
    if start <= 11:
        assert run.walk(lambda m, x, y: m.b(CONTROL) == 0) is not None, "the closing shutters"
        assert run.wait_control(12000), "Robo holds the shutters open"
        assert machine.b(CODE_FLAGS) & 2, "the shutter escape finished"
        assert run.exit_to(LOWER_LAB), "back up the basement ladder"
        assert run.wait_control(), "the lower lab in hand again"
        run.checkpoint("escaped")
    if start <= 12:
        # $0E6 object 0 tests story $3F and the leader's tile row below $27.
        # The scene removes Robo, then sets variable 9 to start the fight.
        assert run.walk(lambda m, x, y: m.b(CONTROL) == 0) is not None, "the R Series scene"
        # The menu byte can briefly look open while battle RAM still holds
        # field data. Require the six initialized R Series stat records too.
        robots = (3, 4, 7, 8, 9, 10)
        assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 2 and all(
            machine.w(0x7E5E32 + 0x80 * slot) == 150 for slot in robots), 12000), "the R Series battle"
        assert list(machine.peek(PARTY, 3)) == [0, 2, 0x80], "Crono and Lucca face the R Series"
        run.checkpoint("battle")
    if start <= 13:
        win(run, args.seed, party=2, policy="r_series")
        run.checkpoint("boss_won")
    if start <= 14:
        assert run.wait_control(20000), "the field back in hand after rescuing Robo"
        assert machine.b(STORY) >= 0x42, "the R Series victory advanced the story"
        run.checkpoint("after")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("the R Series beaten and the field back in hand")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
