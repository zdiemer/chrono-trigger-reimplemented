"""Recover the stolen Moon Stone and collect Lucca's Wondershot and Sunshades."""
from .. import addresses as A, bfs
from ..campaign import Run, route_arguments, run_route
from . import son_of_sun as battle, sun_stone as stone
from .sun_stone_save import board
from .epoch_lavos import DIAL_PERIOD, wait_until
from .save_point import press

SAVE = "oracle/saves/routes/sun-stone-planted.srm"
PHASES = ["loaded", "future", "missing", "present", "recovered", "replaced", "charged", "rewards"]
HEADER = """\
# Sun Stone: discover the theft in 2300 A.D., recover the Moon Stone from Porre's
# mayor in 1000 A.D., replace it in the Sun Keep, and collect it in 2300 A.D.
# Lucca returns home and Taban makes the Wondershot and Sunshades; stop back in control.
# The starting save already includes the Jerky gift to the mayor's ancestor.
# Save: sun-stone-planted slot 1 (oracle/saves/routes), written by sun_stone_save.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

# Walks found on machine copies. Trying a known path first avoids restoring
# the native Epoch transition for every node of an entrance search. Each is
# kept only if it actually enters the requested location; other landings use
# the ordinary search.
APPROACHES = {
    (0xFF, (1216, 144)): "DOWN RIGHT UP RIGHT RIGHT UP",
    (0x40, (1248, 192)): "UP UP UP DOWN DOWN LEFT UP UP RIGHT UP",
    (0x40, (1248, 176)): "UP UP DOWN LEFT LEFT UP RIGHT RIGHT UP",
    (0x32, (320, 880)): "UP RIGHT UP",
    (0x32, (400, 896)): "UP UP LEFT LEFT LEFT LEFT UP",
}


def enter(run, location):
    """Fly to the map's named entrance and search for ground and its door."""
    m = run.machine
    exits = [m.peek(0x7E7E00 + 7 * i, 7) for i in range(128)]
    door = next(e for e in exits if e[0] & 0x80
                and (int.from_bytes(e[3:5], "little") & 0x1FF) == location)
    # Approach the present Keep and Porre from the south: their nearer north
    # or west landing patches are cut off by mountains or forest.
    approach_y = (door[1] & 0x3F) * 16 + (48 if location in (0x40, 0x32) else 0)
    battle.fly_to(run, (door[0] & 0x7F) * 16, approach_y)
    stone.land_near(run)
    run.say("landed for", "%03X" % location, "at", run.world_pos(), "door", door.hex())
    run.checkpoint("landing_%03x" % location)
    wanted = location.to_bytes(2, "little").hex()
    path = APPROACHES.get((location, run.world_pos()))
    moves = None
    if path:
        m.mark(4077)
        for direction in path.split():
            press(m, direction, 4, 16)
        press(m, "A", 30)
        if run.map_id() == wanted:
            moves = path.split()
        else:
            m.restore(4077)
        m.free(4077)
    if moves is None:
        moves, _ = bfs.ow_bfs(m, lambda m, x, y: battle.world_map(m) == wanted,
                             battle.world_pos, battle.world_map, confirm=True, max_nodes=400)
    assert moves is not None, "enter location %03X" % location
    assert run.wait_control() and run.map_id() == wanted
    run.say("entered", "%03X" % location, "frame", m.frame)


def leave(run, world):
    assert run.exit_to(world)
    run.machine.run(240)
    board(run)


def has_item(machine, item):
    return any(i == item and n for i, n in zip(machine.peek(A.INVENTORY_ITEMS, A.INVENTORY_SIZE),
                                               machine.peek(A.INVENTORY_COUNTS, A.INVENTORY_SIZE)))


def walk_after_rewards(run):
    """Keep the first ordinary step that proves the cutscene handed control back."""
    m = run.machine
    origin = run.world_pos()
    m.mark(4078)
    for direction in ("DOWN", "LEFT", "RIGHT", "UP"):
        m.restore(4078)
        press(m, direction, 4, 16)
        if run.world_pos() != origin and run.map_id() == "f001" and m.b(A.OVERWORLD_TRAVEL) == 0:
            m.free(4078)
            return
    m.restore(4078)
    m.free(4078)
    raise AssertionError("the world map back in hand after Taban's rewards")


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    start = run.start_at(PHASES)
    if start <= 0:
        m.run(1620)
        m.run(5, "START")
        m.run(2420 - m.frame)
        assert run.select_slot(0)
        press(m, "A", 120)
        assert run.wait_control() and run.map_id() == "d001"
        assert m.b(stone.QUEST) == 7 and m.b(0x7F01D2) & 4
        assert not has_item(m, 0x28) and not has_item(m, 0xBA), "neither reward is in the starting bag"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.find_player()
        assert run.go(616, 386, tolerance=5), "walk to the Epoch gangplank"
        assert run.walk(lambda m, x, y: m.b(A.DIALOG_OPEN) != 0, max_nodes=1200) is not None, "the boarding question"
        # The native field-to-menu transition keeps the cockpit's location;
        # menu_mode 8 identifies the dial even while MAP_ID remains $1D9.
        assert run.dialogs_until(lambda: run.map_id() == "f701" or m.b(0x7E027C) == 8,
                                 5000), "the Epoch time dial"
        m.run(120)
        for _ in range(8):
            now = m.b(DIAL_PERIOD)
            if now == 1:
                break
            press(m, "DOWN" if now < 1 else "UP", 56, 4)
        assert m.b(DIAL_PERIOD) == 1
        press(m, "A", 100)
        assert run.dialogs_until(lambda: run.map_id() == "f201", 5000)
        m.run(500)
        run.checkpoint("future")
    if start <= 2:
        enter(run, 0xFF)
        assert run.talk(9, done=lambda: m.b(stone.QUEST) == 0x0F), "discover the empty Sun Keep"
        assert run.wait_control()
        run.checkpoint("missing")
    if start <= 3:
        leave(run, "f201")
        stone.travel(run, 3, "f001")
        enter(run, 0x40)
        assert run.talk(9, done=lambda: bool(m.b(0x7F0139) & 1)), "the stone is missing in 1000 A.D. too"
        assert run.wait_control()
        run.checkpoint("present")
    if start <= 4:
        leave(run, "f001")
        enter(run, 0x32)
        assert run.talk(8, done=lambda: m.b(stone.QUEST) == 0x17), "the generous mayor returns the Moon Stone"
        assert run.wait_control() and has_item(m, 0xDE)
        run.checkpoint("recovered")
    if start <= 5:
        leave(run, "f001")
        enter(run, 0x40)
        assert run.talk(9, done=lambda: m.b(stone.QUEST) == 0x37), "replace the Moon Stone"
        assert run.wait_control() and not has_item(m, 0xDE)
        run.checkpoint("replaced")
    if start <= 6:
        leave(run, "f001")
        stone.travel(run, 1, "f201")
        enter(run, 0xFF)
        run.checkpoint("charged")
    if start <= 7:
        assert run.talk(8, done=lambda: bool(m.b(stone.QUEST) & 0x40)), "Lucca takes the Sun Stone"
        assert run.dialogs_until(lambda: m.b(stone.QUEST) == 0xF7, 15000), "Taban makes both rewards"
        assert wait_until(m, lambda: run.map_id() == "f001", 2000)
        m.run(180)
        assert has_item(m, 0x28) and has_item(m, 0xBA), "Wondershot and Sunshades in the bag"
        assert m.b(A.OVERWORLD_TRAVEL) == 0
        walk_after_rewards(run)
        run.checkpoint("rewards")
    assert m.b(stone.QUEST) == 0xF7 and has_item(m, 0x28) and has_item(m, 0xBA)
    m.screenshot(run.prefix + "_end.png")
    run.finish("the Sun Stone quest complete, Wondershot and Sunshades received")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
