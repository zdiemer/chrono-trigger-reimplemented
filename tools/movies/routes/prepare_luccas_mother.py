"""Make a cartridge save before Fiona's campfire, by playing from ct-state11 slot 1.

Hand-run prerequisite for luccas_mother; its long input movie stays outside the
library. Writes <prefix>.srm after saving through the game's menu. No memory
writes: the exported bytes are the cartridge save the game itself just made.
"""
import heapq
from pathlib import Path

from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/technoplaza/ct-state11.srm"
PHASES = ["loaded", "desert", "upper", "upper_won", "lower", "retinite",
          "outside", "forest", "present", "saved"]


def boot(run, slot=0):
    m = run.machine
    m.run(1620)
    m.press("START", 5)
    m.run(2420 - m.frame)
    assert run.select_slot(slot)
    m.press("A", 4, 240)


def world_search(run, goal, max_nodes=250):
    m = run.machine
    moves, _ = bfs.ow_bfs(m, goal, lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)),
                          run._map(), confirm=True, max_nodes=max_nodes)
    assert moves is not None, "the world-map destination"


def board(run):
    world_search(run, lambda m, x, y: m.b(A.OVERWORLD_TRAVEL) != 0)
    run.machine.run(150)
    assert run.machine.b(A.OVERWORLD_TRAVEL) == 2, "aboard the Epoch"


def travel(run, period):
    m = run.machine
    m.press("Y", 4, 100)
    for _ in range(7):
        current = m.b(0x7E0C52)
        if current == period:
            break
        m.press("DOWN" if current < period else "UP", 4, 56)
    assert m.b(0x7E0C52) == period
    m.press("A", 4, 1300)


def dash_to(run, target, goal, max_nodes=2500):
    """Search fine steps and diagonals: ordinary walking loses to the quicksand.

    Copies keep only the winning path. The distance orders the search toward
    the stairs; each candidate still runs on the real collision map.
    """
    m, pos = run.machine, run._pos()
    origin = run.map_id()

    def key():
        x, y = pos(m)
        return int(x) // 3, int(y) // 3, run.map_id()

    m.mark(4000)
    root = key()
    queue, parents, slots = [(0, 0, root)], {root: None}, {root: 4000}
    count, found = 0, None
    directions = ("UP", "LEFT", "RIGHT", "DOWN", "UP+LEFT", "UP+RIGHT", "DOWN+LEFT", "DOWN+RIGHT")
    try:
        while queue and count < max_nodes and found is None:
            _, cost, node = heapq.heappop(queue)
            for direction in directions:
                m.restore(slots[node])
                m.probe(4, direction + "+B")
                child = key()
                if child in parents:
                    continue
                parents[child] = node, direction
                x, y = pos(m)
                if goal(m, x, y):
                    found = child
                    break
                if run.map_id() != origin:
                    continue
                if count >= max_nodes:
                    break
                count += 1
                m.mark(count)
                slots[child] = count
                distance = abs(x - target[0]) + abs(y - target[1])
                heapq.heappush(queue, (cost + 1 + distance / 3, cost + 1, child))
    finally:
        for slot in range(1, count + 1):
            m.free(slot)
        m.restore(4000)
        m.free(4000)
    assert found is not None, "a dash to the stairs"
    moves = []
    while parents[found]:
        found, direction = parents[found]
        moves.append(direction)
    for direction in reversed(moves):
        m.run(4, direction + "+B")


def retinite_policy(bot, state, rng, inventory):
    member = state.member
    marle = policies.party_ids(bot)[member] == 1
    action = policies.survival(state, rng, inventory, member, [0, 1, 2],
                              healer_threshold=.65, self_threshold=.55,
                              cure=(6, 5) if marle else None, life=(7, 15) if marle else None)
    if action:
        return action
    if marle:
        if state.mp(member) < 8:
            action = (policies.use(inventory, policies.ELIXIR, member)
                      or policies.use(inventory, policies.MID_ETHER, member))
            if action:
                return action
        return "tech", 5, None  # Ice 2 weakens the shell; the others attack it.
    living = policies.enemies(state)
    return "attack", min(living, key=lambda i: state.hp(i)) if living else None


def fight(run, policy, seed):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    run.fight(policy, won, seed=seed, max_frames=40000)
    assert run.wait_control(), "the field after the battle"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            boot(run)
            assert run.map_id() == "f001" and m.b(0x7F00F7) & 2
            assert not m.b(0x7F007C) & 0x90 and not m.b(0x7F01A3) & 1
            run.checkpoint("loaded")
        if start <= 1:
            board(run)
            travel(run, 4)
            assert run.map_id() == "f101"
            m.run(210, "DOWN")
            m.run(10)
            m.press("A", 3, 150)
            assert m.b(A.OVERWORLD_TRAVEL) == 0
            world_search(run, lambda m, x, y: run.map_id() == "9f00")
            assert run.wait_control()
            run.checkpoint("desert")
        if start <= 2:
            assert run.exit_to("a000") and run.wait_control()
            run.checkpoint("upper")
        if start <= 3:
            assert run.find_player() is not None
            assert run.walk(lambda m, x, y: y >= 272 or m.b(A.BATTLE_MENU_MEMBER) < 0x80,
                            max_nodes=1200) is not None
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 3000)
            fight(run, "party", args.seed)
            run.checkpoint("upper_won")
        if start <= 4:
            assert run.exit_to("a100") and run.wait_control()
            run.checkpoint("lower")
        if start <= 5:
            assert run.find_player() is not None
            assert run.walk(lambda m, x, y: (112 <= x <= 176 and 176 <= y <= 240)
                            or m.b(A.BATTLE_MENU_MEMBER) < 0x80, max_nodes=1500) is not None
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 4000)
            fight(run, retinite_policy, args.seed)
            assert m.b(0x7F01A3) & 1, "Retinite defeated"
            run.checkpoint("retinite")
        if start <= 6:
            assert run.find_player() is not None
            # Cross the sand's pull before searching the narrow staircase.
            for direction, frames in (("LEFT", 64), ("UP", 120), ("LEFT", 24), ("UP", 60)):
                m.run(frames, direction + "+B")
            dash_to(run, (72, 80), lambda m, x, y: x < 96 and y < 112)
            for direction in ("UP+RIGHT", "UP", "LEFT"):
                m.run(48, direction + "+B")
            assert run.map_id() == "a000" and run.wait_control()
            dash_to(run, (232, 112), lambda m, x, y: 216 <= x <= 248 and y <= 120)
            m.run(64, "UP+B")
            m.run(120)
            assert run.map_id() == "f101"
            run.checkpoint("outside")
        if start <= 7:
            m.press("Y", 3, 90)
            for button in ("DOWN", "A", "RIGHT", "DOWN", "A", "B"):
                m.press(button, 3, 40)
            m.run(150)
            assert m.peek(A.PARTY_MEMBERS, 3) == bytes((0, 3, 1)), "Robo in the active party"
            world_search(run, lambda m, x, y: run.map_id() == "9e00")
            assert run.wait_control() and run.talk(8)
            assert run.dialogs_until(lambda: run.map_id() == "f101", 5000)
            assert m.b(0x7F019E) & 9 == 9, "Robo restoring the forest"
            run.checkpoint("forest")
        if start <= 8:
            m.run(150)
            board(run)
            travel(run, 3)
            assert run.map_id() == "f001"
            m.press("A", 3, 150)
            assert m.b(A.OVERWORLD_TRAVEL) == 0
            run.checkpoint("present")
        if start <= 9:
            m.press("X", 3, 90)
            for _ in range(6):
                if m.b(0x7E0054) == 5:
                    break
                m.press("RIGHT", 3, 30)
            m.press("A", 3, 90)
            assert m.b(0x7E0067) == 6 and m.b(0x7E0054) == 0
            m.press("A", 3, 60)
            m.press("A", 3, 120)
            assert not m.b(0x7F007C) & 0x90, "campfire and rescue still unplayed"
            run.checkpoint("saved")
        Path(run.prefix + ".srm").write_bytes(m.peek(0x306000, 8192))
        run.finish("the cartridge saved before Fiona's campfire")
        if args.movie:
            run.write_movie(args.movie, ["# Hand-run prerequisite for luccas_mother; keep outside the library.",
                                        "# Save: technoplaza ct-state11 slot 1.",
                                        "# frames {FRAMES}", "# sram crc32 {CRC}"])
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
