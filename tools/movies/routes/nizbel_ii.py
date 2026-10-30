"""Through Tyrano Lair from ctsave07 slot 3 to Nizbel II.

The location exits and event scripts supply the itinerary; walks search the
live field, and only the successful input is kept. No memory is changed.
"""
from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route
from . import menu_sweep as menus

SAVE = "oracle/saves/fantasyanime/ctsave07-azala.srm"
SLOT = 2
PHASES = ["loaded", "entrance", "prison", "kino_room", "guards", "kino", "door",
          "switches", "upper_hall", "maze", "lower_stairs", "lower_hall", "door_switch",
          "opened", "boss_room", "rested", "battle", "won", "after"]

HEADER = """\
# Tyrano Lair: free Kino, work through the lair, and beat Nizbel II (route nizbel_ii).
# Save: ctsave07 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def fight(run, seed=1, policy="party_with_lapis"):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    run.fight(policy, won, seed=seed, max_tries=100)
    assert run.wait_control(12000), "field after the fight"


def reach(run, goal, label):
    """Walk to a goal, playing switches and encounters on its approach."""
    m = run.machine
    for _ in range(12):
        assert control(run), "control before walking"
        moves = run.walk(lambda m, x, y: goal(m, x, y)
                         or m.b(A.DIALOG_OPEN) != 0 or m.b(0x7E011F) == 0)
        m.run(60)
        run.say("reach", label, "steps", moves and len(moves), "map", run.map_id(),
                "position", run.me(), "frame", m.frame)
        if goal(m, *run.me()):
            assert control(run), "arrival"
            return
        if not m.b(0x7E011F) or run.dialog():
            run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80 or m.b(0x7E011F) != 0, 6000)
            if m.b(A.BATTLE_MENU_MEMBER) < 0x80:
                fight(run)
        else:
            assert moves is not None, "path to " + label
    raise AssertionError("too many encounters to " + label)


def travel(run, want):
    reach(run, lambda m, x, y: m.peek(A.MAP_ID, 2).hex() == want, want)


def approach(run, x, y):
    reach(run, lambda m, px, py: abs(px - x) <= 4 and abs(py - y) <= 4, str((x, y)))


def control(run):
    """The maze can land at a corner where neither axis moves both ways.

    find_player intentionally rejects that. With the event's control flag
    back, test a step away from the corner and keep it only when the normal
    control check then recognizes the leader.
    """
    if run.wait_control(1200):
        return True
    m = run.machine
    if run.map_id() != "3101" or not m.b(0x7E011F):
        return False
    m.mark(4088)
    for direction in bfs.DIRECTIONS:
        m.restore(4088)
        m.probe(16, direction)
        if run.find_player() is not None:
            m.restore(4088)
            m.free(4088)
            m.run(16, direction)
            return run.wait_control()
    m.restore(4088)
    m.free(4088)
    return False


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    start = run.start_at(PHASES)
    if start > 1 and start != PHASES.index("won"):
        assert control(run), "resumed field in hand"
    if start <= 0:
        m.run(1620)
        m.run(5, "START")
        m.run(2420 - m.frame)
        run.select_slot(SLOT)
        m.run(4, "A")
        for _ in range(200):
            m.run(10)
            if run.map_id() == "f301":
                break
        m.run(120)
        assert run.map_id() == "f301", "65,000,000 B.C. world map"
        run.checkpoint("loaded")
    if start <= 1:
        moves, visited = bfs.ow_bfs(m, lambda m, x, y: m.peek(A.MAP_ID, 2).hex() == "2b01",
                                    lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)),
                                    lambda m: m.peek(A.MAP_ID, 2).hex(), confirm=True, max_nodes=800)
        assert moves is not None, "Tyrano Lair entrance"
        assert run.wait_control(), "entrance in hand"
        run.say("entrance", len(moves), "steps", len(visited), "visited", run.me())
        run.checkpoint("entrance")
    if start <= 2:
        travel(run, "ae01")
        run.checkpoint("prison")
    if start <= 3:
        travel(run, "2e01")
        run.checkpoint("kino_room")
    if start <= 4:
        # $12E object 0 starts the guards at column $37, rows $13-$19.
        assert run.go(55 * 16 + 8, 22 * 16), "Kino's guards"
        assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 6000), "guards attack"
        fight(run, args.seed)
        run.checkpoint("guards")
    if start <= 5:
        # Cross column $37 again after the guards: object 0 now calls Kino
        # forward to the gate. Its collision keeps the usual talk() spot out
        # of reach, but standing below it is within contact.c's 28 pixels.
        assert run.go(55 * 16 + 8, 22 * 16), "Kino approaches"
        m.run(120)
        assert run.go(49 * 16 + 8, 22 * 16), "below Kino's gate"
        m.run(2, "UP")
        m.run(8)
        m.run(3, "A")
        assert run.dialogs_until(lambda: m.b(0x7F0063) & 1, 16000), "free Kino"
        assert run.wait_control(), "Kino released"
        run.checkpoint("kino")
    if start <= 6:
        travel(run, "ae01")
        assert run.dialogs_until(lambda: m.b(0x7F0000) >= 0x98, 6000), "Kino frees the prisoners"
        m.run(600)  # Let Kino and the four prisoners finish their walks out.
        # Split the walk below the left guards: one position-only search
        # from the right doorway prunes the approach needed to head north.
        moves, _ = bfs.bfs_walk(m, lambda m, x, y: int(x) // 12 == 15 and int(y) // 12 == 57,
                                run._pos(), run._map(), cell=12, step=12, max_nodes=2000)
        assert moves is not None, "below the left guards"
        travel(run, "2b01")
        assert run.dialogs_until(lambda: m.b(0x7F005C) & 0x10, 12000), "Kino opens the door"
        assert run.wait_control(), "Kino's farewell"
        run.checkpoint("door")
    if start <= 7:
        travel(run, "2e01")
        run.checkpoint("switches")
    if start <= 8:
        approach(run, 15 * 16 + 8, 17 * 16)
        travel(run, "2a01")
        run.checkpoint("upper_hall")
    if start <= 9:
        travel(run, "3101")
        run.checkpoint("maze")
    if start <= 10:
        # $131's warps separate its floor into islands. Follow the first
        # chain to (29,47), then search specifically for the warp to (15,39)
        # instead of repeatedly taking the nearby return warp at (34,43).
        reach(run, lambda m, x, y: m.w(0x7E0114) == 0x2F1D, "maze corner (29,47)")
        assert control(run), "maze corner"
        assert run.walk(lambda m, x, y: m.w(0x7E0114) == 0x270F) is not None, "maze exit warp"
        m.run(120)
        assert control(run), "maze exit landing"
        travel(run, "2e01")
        run.checkpoint("lower_stairs")
    if start <= 11:
        travel(run, "2a01")
        run.checkpoint("lower_hall")
    if start <= 12:
        travel(run, "2f01")
        run.checkpoint("door_switch")
    if start <= 13:
        # The right switch at (30,23) opens the inner door without the
        # left switch's ambush. The switch beyond, (29,5), opens $130.
        assert run.talk(0x10, done=lambda: m.b(0x7F021A) == 1), "inner door switch"
        tx, ty = run.object_pos(0x12)
        assert run.go(tx, ty + 24), "below the wall switch"
        m.run(2, "UP")
        m.run(8)
        m.run(3, "A")
        assert run.dialogs_until(lambda: m.b(0x7F00ED) & 0x80, 1200), "Nizbel II's door switch"
        run.checkpoint("opened")
    if start <= 14:
        travel(run, "2a01")
        travel(run, "3001")
        run.checkpoint("boss_room")
    if start <= 15:
        # $130 object 11 is the save point at (7,16). Rest before the boss
        # rather than carrying the dungeon's depleted MP into its fight.
        assert run.go(120, 264), "Nizbel II room's save point"
        m.run(30)
        assert m.b(0x7F01CF) & 0x80, "standing on the save point"
        menus.open_menu(m)
        menus.open_option(m, menus.ITEMS)
        count = menus.count_of(m, menus.SHELTER)
        assert count, "a Shelter for the party"
        menus.use_item(run, menus.SHELTER, choose_member=True)
        m.run(180)
        assert menus.count_of(m, menus.SHELTER) == count - 1, "the Shelter used"
        menus.back_to_main_menu(m)
        menus.press(m, "B", 60)
        assert run.wait_control(), "field after resting"
        run.checkpoint("rested")
    if start <= 16:
        # The far end's objects 9 and 10 start formation $92. The defeated
        # story bit is set BEFORE the fight, so it is not a victory check.
        assert run.walk(lambda m, x, y: m.b(0x7F00EE) & 1) is not None, "Nizbel II's challenge"
        assert run.dialogs_until(lambda: m.w(A.BATTLE_STATS + 3 * A.BATTLE_STAT_STRIDE + 2) == 6500
                                 and m.b(A.BATTLE_MENU_MEMBER) < 3, 8000), "Nizbel II battle"
        run.checkpoint("battle")
    if start <= 17:
        assert run.dialogs_until(lambda: m.w(A.BATTLE_STATS + 3 * A.BATTLE_STAT_STRIDE + 2) == 6500
                                 and m.b(A.BATTLE_MENU_MEMBER) < 3, 8000), "Nizbel II ready"
        fight(run, args.seed, "nizbel_ii")
        run.checkpoint("won")
    if start <= 18:
        assert run.wait_control(12000), "field after Nizbel II"
        assert run.map_id() == "3001" and m.b(0x7F00EE) & 1, "Nizbel II defeated"
        run.checkpoint("after")
    run.finish("Nizbel II defeated and the field back in hand")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.say("end", run.map_id(), run.me(), "story", hex(m.b(0x7F0000)))
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
