"""Reach Giant's Claw's save point from ctsave13 slot 1 (a hand-run approach).

Slot 2 is already after Rust Tyrano and the shell's delivery: $7F:01D2 bit 6,
$7F:00A9 bit 7 and $7F:00A1 bit 6 are set. Slot 1 has all three clear.
The approach is deliberately outside the movie library; rust_tyrano starts
from the cartridge save made at the dungeon's own save point.
"""
from pathlib import Path

from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route
from . import menu_sweep, save_point

SAVE = "oracle/saves/fantasyanime/ctsave13-side_quests2.srm"
CONTROL = 0x7E011F
HEADER = """# Giant's Claw approach: Epoch to 600 A.D., the ruined throne, encounters,
# floor switches and pits, then a Shelter and a cartridge save near Rust Tyrano.
# Hand-run route rainbow_shell; keep this long movie outside the library.
# Save: ctsave13 slot 1 (oracle/saves/fantasyanime/ctsave13-side_quests2.srm).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def dash_walk(run, goal):
    """Search the tight lower hall with the run button held."""
    directions = bfs.DIRECTIONS
    try:
        bfs.DIRECTIONS = tuple(d + "+B" for d in directions)
        return run.walk(goal)
    finally:
        bfs.DIRECTIONS = directions


def fight(run, policy="party_with_lapis", seed=1):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    assert run.dialogs_until(lambda: run.machine.b(A.BATTLE_MENU_MEMBER) < 0x80, 3000), "battle menu"
    run.fight(policy, won, seed=seed, max_frames=30000, max_tries=80)
    assert run.wait_control(12000), "field after battle"


def physical(bot, state, rng, inventory):
    action = policies.survival(state, rng, inventory, state.member, range(3), self_threshold=.5)
    return action or ("attack", min(policies.enemies(state), key=state.hp))


def without_fire(bot, state, rng, inventory):
    action = policies.party_with_lapis(bot, state, rng, inventory)
    if action[0] == "tech" and policies.party_ids(bot)[state.member] == 2:
        return ("attack", min(policies.enemies(state), key=state.hp))
    return action


def enter(run, location):
    assert run.exit_to(location), "enter " + location
    assert run.wait_control(), "control in " + location


def encounter_on_the_way(run, location):
    assert run.wait_control()
    assert run.walk(lambda m, x, y: run.map_id() == location or not m.b(CONTROL)) is not None
    run.machine.run(300)


def load(run, slot=0):
    m = run.machine
    m.run(1620)
    m.run(5, "START")
    m.run(2420 - m.frame)
    assert run.select_slot(slot)
    m.run(4, "A")
    m.run(450)


def approach(run):
    m = run.machine

    def travel():
        load(run)
        assert run.map_id() == "f001"
        assert not m.b(0x7F01D2) & 0x40
        m.run(3, "A")                 # the Epoch is at the saved position
        m.run(600)
        assert m.b(A.OVERWORLD_TRAVEL) == 2
        m.run(4, "Y")
        m.run(100)
        m.run(4, "DOWN")
        m.run(56)
        assert m.b(0x7E0C52) == 4       # 600 A.D.
        m.run(4, "A")
        m.run(1800)
        assert run.map_id() == "f101"
        m.run(292, "RIGHT")
        m.run(64, "UP")
        m.run(30)
        m.run(8, "RIGHT")
        m.run(4, "DOWN")
        m.run(20)
        m.run(3, "A")
        m.run(300)
        assert m.b(A.OVERWORLD_TRAVEL) == 0
        assert run.world_exit(max_nodes=100)
        assert run.wait_control()
        assert run.map_id() == "c300"

    def throne():
        enter(run, "2901")
        assert run.walk(lambda m, x, y: y >= 15 * 16) is not None
        assert run.wait_control()
        assert run.walk(lambda m, x, y: y >= 27 * 16) is not None
        enter(run, "c300")

    def first_fight():
        encounter_on_the_way(run, "6e00")
        fight(run)

    def second_fight():
        encounter_on_the_way(run, "6e00")
        fight(run)
        enter(run, "6e00")

    def pit():
        assert run.go(168, 424)
        assert run.wait_control()
        m.run(24, "UP")
        m.run(24, "LEFT")
        m.run(300)
        assert run.wait_control()
        assert run.me()[0] > 400
        enter(run, "c400")

    def lower_fights():
        assert run.walk(lambda m, x, y: y < 300 or not m.b(CONTROL)) is not None
        m.run(300)
        fight(run, without_fire)
        assert run.walk(lambda m, x, y: y < 300 or not m.b(CONTROL)) is not None
        m.run(300)
        fight(run, physical)

    def lower_exit():
        # This door reloads the same location. Let the fade finish before
        # interpreting positions in the new part of its map.
        m.run(40, "RIGHT")
        m.run(100, "DOWN")
        m.run(300)
        assert run.wait_control()
        assert run.me()[1] < 300
        enter(run, "2801")

    def hall():
        encounter_on_the_way(run, "6d00")
        fight(run)
        enter(run, "6d00")
        assert run.go(440, 264)
        assert run.wait_control()

    def upper():
        enter(run, "c300")
        assert run.walk(lambda m, x, y: x > 330 or not m.b(CONTROL)) is not None
        m.run(300)
        assert m.b(CONTROL), "past the upper cave's enemies"
        enter(run, "6d00")

    def pit_switch():
        assert run.go(872, 658)
        m.run(2, "UP")
        m.run(8)
        m.run(3, "A")
        m.run(600)
        assert run.wait_control()
        assert run.me()[0] < 500

    def hall_dash():
        assert dash_walk(run, lambda m, x, y: x > 500) is not None
        assert dash_walk(run, lambda m, x, y: abs(x - 536) <= 4 and abs(y - 592) <= 4) is not None
        m.run(120, "UP+B")
        m.run(240)
        m.run(30, "RIGHT+B")
        m.run(300)
        assert run.wait_control()
        assert run.me()[0] > 900

    def save_room():
        assert run.go(984, 344)
        m.run(8, "DOWN")
        m.run(20)
        assert m.b(0x7F01CF) & 0x80, "standing on the save point"
        menu_sweep.open_menu(m)
        menu_sweep.open_option(m, menu_sweep.ITEMS)
        menu_sweep.use_item(run, menu_sweep.SHELTER, choose_member=True)
        menu_sweep.back_to_main_menu(m)
        menu_sweep.press(m, "B", 60)
        assert run.wait_control()

    def saved():
        m.run(30, "A")
        m.run(100)
        assert m.b(save_point.MENU_SCREEN) == save_point.SAVE_POINT_SCREEN
        save_point.to_slot(m, 0)
        save_point.press(m, "A", 60)
        save_point.press(m, "A", 90)
        save_point.press(m, "B", 60)
        assert run.wait_control()
        assert not m.b(0x7F01D2) & 0x40, "Rust Tyrano still alive"

    phases = [("entrance", travel), ("throne", throne), ("first_fight", first_fight),
              ("second_fight", second_fight), ("pit", pit), ("lower_fights", lower_fights),
              ("lower_exit", lower_exit), ("hall", hall), ("upper", upper),
              ("pit_switch", pit_switch), ("hall_dash", hall_dash),
              ("save_room", save_room), ("saved", saved)]
    start = run.start_at([name for name, _ in phases])
    for index, (name, step) in enumerate(phases):
        if index >= start:
            step()
            run.checkpoint(name)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        approach(run)
        # This is cartridge SRAM read after the game's own save operation,
        # not a machine state. The source archive is never overwritten.
        Path(run.prefix + ".srm").write_bytes(run.machine.peek(0x306000, 8192))
        run.finish("saved at Giant's Claw before Rust Tyrano")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
