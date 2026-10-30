"""Restore the Masamune at Cyrus's grave, including the Choras repairs.

ctsave14 slot 1 has no repairs ($7F:019F = 0), no restored-Masamune flag
($7F:01A3 bit 6), and Frog's original sword ($7E:2769 = $3D). Slot 2 is
after this quest. Only the left wing needs clearing and a paid repair to
reach the grave; the right wing's treasure rooms are outside this route.
"""
import heapq

from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
HEADER = """\
# Cyrus's grave: borrow the carpenter's tools across the eras, repair the Northern Ruins,
# clear the left wing, and bring Frog to Cyrus; the Masamune restored and the field back in hand.
# Route: cyrus. Save: ctsave14 slot 1 (oracle/saves/fantasyanime).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
TOOLS, REPAIRS, CYRUS, SWORD = 0x7F019E, 0x7F019F, 0x7F01A3, 0x7E2769
PHASES = ["loaded", "asked", "tools", "repaired", "leftwing", "first", "second", "cleared", "paid",
          "frog", "grave", "restored"]


def approach(run, x, y, world=False, max_nodes=600, target=None):
    """Search machine copies towards a waypoint; keep only the successful walk.

    A distance heuristic avoids exploring a whole room for an NPC's one
    reachable side. Positions remain observations of the running game, not
    a collision map. The eight-pixel world cells retain half-tile arrivals.
    """
    machine = run.machine
    position = run.world_pos if world else run.me
    here, begin, root = run.map_id(), machine.frame, 4080
    machine.mark(root)
    queue = [(0, 0, root, position(), [])]
    seen, slots = set(), []
    cell, tolerance = (8, 8) if world else (6, 5)
    hold, rest = (16, 4) if world else (8, 0)
    try:
        while queue and len(seen) < max_nodes:
            _, cost, slot, point, path = heapq.heappop(queue)
            key = tuple(int(p) // cell for p in point)
            if key in seen:
                continue
            seen.add(key)
            if abs(point[0] - x) <= tolerance and abs(point[1] - y) <= tolerance:
                machine.restore(root)
                machine.frame = begin
                for direction in path:
                    machine.run(hold, direction)
                    machine.run(rest)
                return
            for direction in ("UP", "DOWN", "LEFT", "RIGHT"):
                machine.restore(slot)
                machine.probe(hold, direction)
                machine.probe(rest)
                point2 = position()
                key2 = tuple(int(p) // cell for p in point2)
                if run.map_id() != here:
                    if run.map_id() == target:
                        machine.restore(root)
                        machine.frame = begin
                        for move in path + [direction]:
                            machine.run(hold, move)
                            machine.run(rest)
                        return
                    continue
                if key2 in seen:
                    continue
                assert len(slots) < 3900, "waypoint search slots exhausted"
                saved = len(slots) + 10
                slots.append(saved)
                machine.mark(saved)
                distance = (abs(point2[0] - x) + abs(point2[1] - y)) / cell
                heapq.heappush(queue, (cost + 1 + distance, cost + 1, saved, point2, path + [direction]))
        machine.restore(root)
        machine.frame = begin
        raise AssertionError("unreachable waypoint %s on %s" % ((x, y), here))
    finally:
        for slot in slots:
            machine.free(slot)
        machine.free(root)


def press(machine, button, gap=40):
    machine.run(3, button)
    machine.run(gap)


def world_search(run, goal, max_nodes=150):
    machine = run.machine
    moves, _ = bfs.ow_bfs(machine, goal,
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(),
                          confirm=True, max_nodes=max_nodes)
    assert moves is not None, "world-map destination"


def board(run):
    machine = run.machine
    press(machine, "A")
    if machine.b(A.OVERWORLD_TRAVEL) == 0:
        world_search(run, lambda m, x, y: m.b(A.OVERWORLD_TRAVEL) != 0)
    machine.run(180)
    assert machine.b(A.OVERWORLD_TRAVEL) == 2, "aboard the Epoch"


def era(run, period):
    machine = run.machine
    press(machine, "Y", 100)
    for _ in range(8):
        current = machine.b(0x7E0C52)
        if current == period:
            break
        press(machine, "DOWN" if current < period else "UP", 56)
    assert machine.b(0x7E0C52) == period, "time dial"
    press(machine, "A", 1500)
    assert run.map_id() == ("f001" if period == 3 else "f101"), "arrival era"


def fly(run, x, y):
    machine = run.machine
    for _ in range(2000):
        px, py = run.world_pos()
        if abs(px - x) <= 5 and abs(py - y) <= 5:
            break
        direction = ("RIGHT" if px < x else "LEFT") if abs(px - x) > 5 else ("DOWN" if py < y else "UP")
        machine.run(1, direction)
    else:
        raise AssertionError("Epoch flight waypoint")
    machine.run(30)


def land(run):
    press(run.machine, "A", 150)
    assert run.machine.b(A.OVERWORLD_TRAVEL) == 0, "Epoch landed"


def enter(run, location, x, y):
    approach(run, x, y, world=True)
    press(run.machine, "A", 150)
    if run.machine.w(A.MAP_ID) != location:
        world_search(run, lambda m, px, py: m.w(A.MAP_ID) == location)
        run.machine.run(60)
    assert run.wait_control(), "field in hand"
    assert run.machine.w(A.MAP_ID) == location, "entered the requested location"


def talk_from(run, x, y, facing):
    approach(run, x, y, max_nodes=300)
    run.machine.run(2, facing)
    press(run.machine, "A")
    assert run.wait_control(), "conversation finished"


def leave(run, x, y, world):
    approach(run, x, y, max_nodes=300)
    run.machine.run(40, "DOWN")
    run.machine.run(240)
    assert run.map_id() == world, "back on the world map"


def door(run, x, y, target):
    approach(run, x, y, target=target)
    run.machine.run(60)
    assert run.wait_control() and run.map_id() == target, "through the door"


def guardians(run, direction, variable, value, seed):
    machine = run.machine
    for _ in range(100):
        if machine.b(variable) == value:
            break
        machine.run(8, direction)
    assert machine.b(variable) == value, "guardian encounter triggered"
    assert run.dialogs_until(lambda: machine.b(A.BATTLE_MENU_MEMBER) < 0x80, 4000), "battle menu"
    seen = []

    def won(r, state):
        if policies.enemies(state):
            seen.append(True)
            return False
        return bool(seen) and any(state.hp(slot) for slot in range(3))

    run.fight("party", won, seed=seed, max_frames=16000)
    assert run.wait_control(10000), "guardians beaten, field back in hand"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)
    if start > 1:
        assert run.find_player() is not None, "resumed field in hand"
    if start <= 0:
        machine.run(1620)
        press(machine, "START", 797)
        assert run.select_slot(0)
        press(machine, "A", 500)
        assert run.map_id() == "f001"
        assert machine.b(REPAIRS) == 0 and not machine.b(CYRUS) & 0x40
        assert machine.b(SWORD) == 0x3D
        run.checkpoint("loaded")
    if start <= 1:
        board(run)
        era(run, 4)
        fly(run, 1240, 776)
        land(run)
        enter(run, 0xBC, 1176, 824)
        talk_from(run, 582, 303, "RIGHT")
        assert machine.b(TOOLS) & 0x10, "the carpenter needs tools"
        run.checkpoint("asked")
    if start <= 2:
        leave(run, 568, 335, "f101")
        approach(run, 1240, 776, world=True)
        board(run)
        era(run, 3)
        land(run)
        enter(run, 0x3E, 1176, 840)
        talk_from(run, 196, 420, "UP")
        assert machine.b(TOOLS) & 0x20, "the tools loan agreed"
        leave(run, 248, 463, "f001")
        enter(run, 0x3D, 1144, 856)
        talk_from(run, 328, 935, "UP")
        assert machine.b(TOOLS) & 0x80, "tools collected"
        run.checkpoint("tools")
    if start <= 3:
        leave(run, 320, 975, "f001")
        approach(run, 1240, 776, world=True)
        board(run)
        era(run, 4)
        land(run)
        enter(run, 0xBC, 1176, 824)
        talk_from(run, 582, 303, "RIGHT")
        assert machine.b(TOOLS) & 0x40, "tools delivered"
        leave(run, 568, 335, "f101")
        enter(run, 0xBA, 1144, 856)
        talk_from(run, 136, 711, "UP")
        assert machine.b(REPAIRS) & 2, "first repairs"
        run.checkpoint("repaired")
    if start <= 4:
        leave(run, 176, 735, "f101")
        enter(run, 0x41, 1240, 696)
        door(run, 136, 80, "4200")
        run.checkpoint("leftwing")
    if start <= 5:
        guardians(run, "DOWN", 0x7F0216, 1, args.seed)
        run.checkpoint("first")
    if start <= 6:
        guardians(run, "RIGHT", 0x7F0218, 1, args.seed)
        run.checkpoint("second")
    if start <= 7:
        guardians(run, "LEFT", 0x7F021A, 3, args.seed)
        assert machine.b(REPAIRS) & 0x20, "left wing permanently cleared"
        run.checkpoint("cleared")
    if start <= 8:
        door(run, 88, 312, "4100")
        leave(run, 256, 191, "f101")
        enter(run, 0xBA, 1144, 856)
        talk_from(run, 136, 711, "UP")
        assert machine.b(REPAIRS) & 4, "left floor repaired"
        run.checkpoint("paid")
    if start <= 9:
        # The field's party exchange: replace Crono with the first reserve,
        # Frog. The main menu's exchange only reorders the active three.
        press(machine, "Y", 100)
        assert machine.b(0x7E0067) == 7, "party exchange"
        assert machine.b(A.PARTY_MEMBERS + 4) == 4, "Frog first in reserve"
        press(machine, "A")
        press(machine, "RIGHT")
        press(machine, "A", 80)
        press(machine, "B", 100)
        assert run.wait_control() and machine.b(A.PARTY_MEMBERS) == 4, "Frog leads"
        run.checkpoint("frog")
    if start <= 10:
        leave(run, 176, 735, "f101")
        enter(run, 0x41, 1240, 696)
        door(run, 136, 80, "4200")
        door(run, 392, 312, "4900")
        run.checkpoint("grave")
    if start <= 11:
        approach(run, 648, 695, max_nodes=700)
        machine.run(2, "UP")
        press(machine, "A")
        assert run.dialogs_until(lambda: bool(machine.b(CYRUS) & 0x40), 12000, press_every=30), "Cyrus at rest"
        assert run.wait_control(6000), "field back in hand"
        assert machine.b(SWORD) == 0x42, "Masamune restored"
        run.checkpoint("restored")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("Cyrus at rest, Masamune restored, field in hand")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
