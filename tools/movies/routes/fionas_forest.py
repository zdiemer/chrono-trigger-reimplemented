"""Restore Fiona's forest: the Sunken Desert, Robo's work and the campfire.

Save: ctsave12 slot 3. Retinite's defeated flag ($7F:01A3 bit 0) and
the forest flags ($7F:019E) are clear; ctsave13 slot 1 has them set.
"""
import heapq

from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route
from .tech_sweep_spekkio import move_to, party_record, reserve, press

SAVE = "oracle/saves/fantasyanime/ctsave12-side_quests1.srm"
SLOT = 2
PHASES = ["loaded", "prepared", "village", "seed", "desert", "lower", "battle", "won", "forest", "shrine", "camp"]
WORLD_600, WORLD_1000 = "f101", "f001"
FIONA, DESERT_ENTRY, DESERT, RETINITE = "9e00", "9f00", "a000", "a100"
SHRINE, CAMP = "3900", "b401"
FOREST = 0x7F019E
DESERT_FLAGS = 0x7F01A3

HEADER = """\
# Fiona's forest: the Sunken Desert and Retinite, Robo left to replant the forest in 600 A.D.,
# his recovery at the shrine 400 years later, and the campfire, ending with Lucca in hand
# in 1000 A.D. before her optional trip to the past (route fionas_forest).
# Save: ctsave12 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def approach(run, x, y, tolerance=6, limit=700, goal=None, dash=False):
    """Search toward a known landmark, retaining only the successful walk.

    The priority is distance to the landmark; walls and moving NPCs are still
    tested by the machine. Each saved node is discarded before replaying.
    """
    m = run.machine
    here = run.map_id()
    m.mark(4000)
    queue = [(0, 4000, [])]
    seen = set()
    slots = []
    found = None
    try:
        while queue and len(slots) < limit:
            _, slot, path = heapq.heappop(queue)
            for direction in ("UP", "DOWN", "LEFT", "RIGHT", "UP+LEFT", "UP+RIGHT", "DOWN+LEFT", "DOWN+RIGHT"):
                if dash:
                    direction = "B+" + direction
                m.restore(slot)
                m.probe(8, direction)
                px, py = run.me()
                if (goal and goal()) or (not goal and run.map_id() == here
                                        and abs(px - x) <= tolerance and abs(py - y) <= tolerance):
                    found = path + [direction]
                    break
                key = (int(px) // 4, int(py) // 4)
                if run.map_id() != here or key in seen:
                    continue
                seen.add(key)
                child = len(slots) + 1
                m.mark(child)
                slots.append(child)
                heapq.heappush(queue, (abs(px - x) + abs(py - y), child, path + [direction]))
            if found is not None:
                break
    finally:
        m.restore(4000)
        m.free(4000)
        for slot in slots:
            m.free(slot)
    if found is not None:
        for direction in found:
            m.run(8, direction)
    run.say("approach", (x, y), "steps", None if found is None else len(found), "at", run.me())
    return found is not None



def world_search(run, goal, limit=250):
    m = run.machine
    run.wait_world_control()
    moves, _ = bfs.ow_bfs(m, lambda m, x, y: goal(),
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(),
                          confirm=True, max_nodes=limit)
    assert moves is not None, "the world-map destination"


def board(run):
    m = run.machine
    world_search(run, lambda: m.b(A.OVERWORLD_TRAVEL) != 0)
    for _ in range(40):
        m.run(5)
        if m.b(A.OVERWORLD_TRAVEL) == 2:
            break
    assert m.b(A.OVERWORLD_TRAVEL) == 2, "aboard the Epoch"
    m.run(180)


def travel(run, period, world):
    m = run.machine
    press(m, "Y", 100)
    for _ in range(7):
        shown = m.b(0x7E0C52)
        if shown == period:
            break
        press(m, "DOWN" if shown < period else "UP", 60)
    assert m.b(0x7E0C52) == period, "the Epoch's time dial"
    press(m, "A", 100)
    ready = False
    for _ in range(150):
        m.run(20)
        if run.map_id() != world or m.b(A.OVERWORLD_TRAVEL) != 2:
            continue
        # The destination id changes before the scene is finished. Check that
        # the Epoch actually answers the pad, restoring the probe afterwards.
        m.mark(4000)
        m.probe(8)
        baseline = run.world_pos()
        m.restore(4000)
        m.probe(8, "RIGHT")
        ready = run.world_pos() != baseline
        m.restore(4000)
        m.free(4000)
        if ready:
            break
    assert ready, "the destination's Epoch back in hand"
    m.run(40)


def fly_land(run, x, y):
    m = run.machine
    for axis, target, minus, plus in [(0, x, "LEFT", "RIGHT"), (1, y, "UP", "DOWN")]:
        for _ in range(1100):
            at = run.world_pos()[axis]
            if abs(at - target) <= 1:
                break
            m.run(1, plus if at < target else minus)
        else:
            raise AssertionError("the Epoch reaches its landing area")
    m.run(10)
    press(m, "A", 180)
    assert m.b(A.OVERWORLD_TRAVEL) == 0, "the Epoch lands"


def enter(run, location):
    world_search(run, lambda: run.map_id() == location)
    run.machine.run(100)
    assert run.wait_control(), "the location in hand"


def prepare(run):
    from .menu_sweep import open_menu, open_option, back_to_main_menu, SETTINGS, EQUIPMENT
    m = run.machine
    open_menu(m)
    open_option(m, SETTINGS)
    press(m, "RIGHT")
    press(m, "A")
    assert m.b(0x7E0054) == 10, "the battle speed meter"
    press(m, "DOWN", 20)
    for _ in range(8):
        if m.b(0x7E0D90) == 0:
            break
        press(m, "LEFT", 20)
    assert m.b(0x7E0D90) == 0, "the fastest battle messages"
    back_to_main_menu(m)
    open_option(m, EQUIPMENT)
    press(m, "A", 40)
    press(m, "A", 40)
    press(m, "A", 40)  # VedicBlade, the first and strongest sword in the bag
    assert m.b(0x7E2629) == 12, "Crono equips the VedicBlade"
    back_to_main_menu(m)
    press(m, "B", 80)


def bring_robo(run):
    m = run.machine
    press(m, "Y", 100)
    assert m.b(0x7E0067) == 7, "the party exchange"
    assert move_to(m, 1)
    press(m, "A")
    assert move_to(m, 3 + reserve(party_record(m)).index(3))
    press(m, "A", 60)
    press(m, "B", 90)
    assert party_record(m)[:3] == [0, 3, 1], "Crono, Robo and Marle"
    assert run.wait_control()


def retinite_policy(bot, state, rng, inventory):
    """Ice 2 softens both halves; leave the core alive until both are gone.

    Marle keeps casting water while Robo supplies the party's healing. Killing
    the upper half first and keeping the core alive avoids the fiercer attacks
    that follow the core's death.
    """
    member = state.member
    who = policies.party_ids(bot)[member]
    for ally in range(3):
        if not state.hp(ally):
            return policies.use(inventory, policies.REVIVE, ally) or ("attack", None)
    if who == 1:
        if state.mp(member) >= 8:
            return ("tech", 5, None)
        return (policies.use(inventory, policies.FULL_ETHER, member)
                or policies.use(inventory, policies.ETHER, member) or ("attack", None))
    if who == 3 and min(policies.fraction(state, ally) for ally in range(3)) < .35:
        return policies.use(inventory, policies.MEGA_ELIXIR, None) or ("tech", 4, None)
    if state.mp(member) < 12:
        action = (policies.use(inventory, policies.FULL_ETHER, member)
                  or policies.use(inventory, policies.ETHER, member))
        if action:
            return action
    living = policies.enemies(state)
    target = next((slot for slot in (9, 6, 3) if slot in living), None)
    if who == 0 and state.mp(member) >= 12:
        return ("tech", 6, target)
    if who == 3 and state.mp(member) >= 12:
        return ("tech", 5, target)
    return ("attack", target)


def fight(run, policy, seed):
    m = run.machine
    assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 3
                             and m.w(A.BATTLE_STATS) > 0 and m.w(A.BATTLE_STATS + 0x80) > 0
                             and m.w(A.BATTLE_STATS + 0x100) > 0, 5000), "the battle's menu and stats"
    if policy is retinite_policy:
        assert run.dialogs_until(lambda: m.w(A.BATTLE_STATS + 9 * 0x80) == 5000, 2000), "Retinite's stats loaded"
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(slot) for slot in range(3))

    run.fight(policy, won, seed=seed, max_frames=24000, max_tries=50)
    assert run.wait_control(), "the field back after the fight"


def catch_retinite(run):
    m = run.machine
    assert approach(run, 152, 216, dash=True, goal=lambda: m.b(0x7E011F) == 0, limit=300)
    assert run.wait_control(), "Retinite's introduction over"
    assert m.b(DESERT_FLAGS) & 4, "Retinite has appeared"
    assert approach(run, 296, 184, dash=True, tolerance=10, limit=300)
    # Hold the upper-right emergence area against the moving sand. The event
    # opens each of four areas in turn according to its random choice.
    for _ in range(1500):
        if (m.b(A.BATTLE_MENU_MEMBER) < 3 and m.w(A.BATTLE_STATS) > 0
                and m.w(A.BATTLE_STATS + 0x80) > 0 and m.w(A.BATTLE_STATS + 0x100) > 0):
            return
        if run.dialog():
            press(m, "A", 37)
            continue
        x, y = run.me()
        button = 0
        if m.b(0x7E011F) == 1:
            if abs(x - 296) > 8:
                button = "B+RIGHT" if x < 296 else "B+LEFT"
            elif abs(y - 184) > 8:
                button = "B+DOWN" if y < 184 else "B+UP"
        m.run(4, button)
    raise AssertionError("Retinite caught at an emergence")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(SLOT)
            m.run(4, "A")
            m.run(400)
            assert run.map_id() == WORLD_600
            assert m.b(FOREST) == 0 and not m.b(DESERT_FLAGS) & 1
            assert not m.b(0x7F00F7) & 2, "the sapling choice is still needed"
            run.checkpoint("loaded")
        if start <= 1:
            prepare(run)
            run.checkpoint("prepared")
        if start <= 2:
            board(run)
            travel(run, 5, "f601")
            fly_land(run, 1200, 416)
            enter(run, "a801")
            run.checkpoint("village")
        if start <= 3:
            assert run.wait_control()
            assert approach(run, 146, 384, tolerance=5)
            press(m, "RIGHT", 8, 2)
            press(m, "A", 60)
            assert run.dialogs_until(lambda: m.b(0x7E0162) == 2, 2000)
            press(m, "DOWN", 10)  # Plant it, with hope.
            press(m, "A", 40)
            assert run.dialogs_until(lambda: m.b(0x7F00F7) & 2, 2000)
            assert run.wait_control()
            bring_robo(run)
            run.checkpoint("seed")
        if start <= 4:
            assert run.wait_control()
            assert approach(run, 72, 520, goal=lambda: run.map_id() == "f601")
            m.run(100)
            board(run)
            travel(run, 4, WORLD_600)
            fly_land(run, 408, 696)
            enter(run, DESERT_ENTRY)
            assert approach(run, 384, 120, goal=lambda: run.map_id() == DESERT, limit=150)
            m.run(120)
            assert run.wait_control()
            run.checkpoint("desert")
        if start <= 5:
            assert run.wait_control()
            # Between the two upper ambushes, then across the mandatory row.
            assert approach(run, 280, 256, dash=True, tolerance=8, limit=300)
            for _ in range(40):
                m.run(4, "B+DOWN")
                if m.b(0x7E011F) == 0:
                    break
            assert m.b(0x7E011F) == 0, "the central desert encounter"
            fight(run, "party", args.seed)
            assert approach(run, 176, 408, dash=True, limit=500)
            assert approach(run, 256, 488, dash=True, goal=lambda: run.map_id() == RETINITE, limit=500)
            m.run(150)
            assert run.wait_control()
            run.checkpoint("lower")
        if start <= 6:
            assert run.wait_control()
            catch_retinite(run)
            run.checkpoint("battle")
        if start <= 7:
            fight(run, retinite_policy, args.seed)
            assert m.b(DESERT_FLAGS) & 1, "Retinite defeated"
            run.checkpoint("won")
        if start <= 8:
            assert run.wait_control()
            assert approach(run, 88, 200, dash=True, limit=300)
            assert approach(run, 80, 90, dash=True, goal=lambda: run.map_id() == DESERT, limit=700)
            m.run(100)
            assert run.wait_control()
            assert approach(run, 232, 96, dash=True, goal=lambda: run.map_id() == WORLD_600, limit=800)
            m.run(150)
            enter(run, FIONA)
            x, y = run.object_pos(8)
            assert approach(run, x - 20, y)
            press(m, "RIGHT", 8, 2)
            press(m, "A", 40)
            assert run.dialogs_until(lambda: m.b(0x7E0162) == 2, 3000)
            press(m, "A", 40)  # Robo may stay.
            assert run.dialogs_until(lambda: run.map_id() == WORLD_600, 3000)
            m.run(600)
            assert m.b(FOREST) & 1 and 3 not in party_record(m)[:3]
            run.checkpoint("forest")
        if start <= 9:
            board(run)
            travel(run, 3, WORLD_1000)
            press(m, "A", 180)
            assert m.b(A.OVERWORLD_TRAVEL) == 0
            enter(run, SHRINE)
            run.checkpoint("shrine")
        if start <= 10:
            assert run.wait_control()
            x, y = run.object_pos(8)
            assert approach(run, x, y + 20, tolerance=8, dash=True)
            press(m, "UP", 8, 2)
            press(m, "A", 40)
            assert run.dialogs_until(lambda: run.map_id() == CAMP, 6000)
            assert run.wait_control(12000)
            run.checkpoint("camp")
        assert run.map_id() == CAMP and m.b(FOREST) & 7 == 7
        assert m.b(0x7F007C) & 0x10 and m.b(0x7F006D) & 1
        assert run.find_player() == 3, "Lucca in hand after the campfire"
        m.screenshot(run.prefix + "_end.png")
        run.finish("Fiona's forest restored and the campfire over")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if m.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
