"""Climb Death Peak through two Lavos Spawn to the final save point.

ctsave12 slot 1 has the Chrono Trigger, clone and Keeper's instructions, and
all three Lavos Spawn still alive. Walking searches restore machine copies;
only the winning path and battle decisions become movie input.
"""
import heapq
import itertools
from pathlib import Path

from .. import addresses as A, policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave12-side_quests1.srm"
PHASES = ["loaded", "cave", "spawn1", "ledge", "east_cave", "east_outside", "opened",
          "east_back", "ledge_back", "first_back", "west_back", "middle", "spawn2_room", "spawn2",
          "ridge", "ridge_crossed", "lower_slope", "last_field", "saved"]
HEADER = """\
# Death Peak, 2300 A.D.: beat the first two Lavos Spawn, open the middle cave, cross the slippery ridge,
# and save before the final Spawn; field control returns (route death_peak).
# Save: ctsave12 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def walk_to(run, x, y, destination=None, limit=900, step=12):
    """Search toward a landmark, using the machine for collisions and exits.

    Prioritize places near the landmark without assuming the space between
    them is walkable. An exit must also be near the landmark: several doors
    lead to different sections of the same map.
    """
    m = run.machine
    origin = run.map_id()
    m.mark(3999)
    serial = itertools.count()
    queue, seen, saved = [], set(), []
    paths = {3999: []}

    def distance(px, py):
        return abs(px - x) + abs(py - y)

    heapq.heappush(queue, (distance(*run.me()) / 24, next(serial), 3999))
    winner = None
    try:
        while queue and len(saved) < limit:
            _, _, slot = heapq.heappop(queue)
            for buttons in ("UP+B", "LEFT+B", "RIGHT+B", "DOWN+B", "UP+LEFT+B",
                            "UP+RIGHT+B", "DOWN+LEFT+B", "DOWN+RIGHT+B"):
                m.restore(slot)
                m.probe(step, buttons)
                px, py = run.me()
                # An exit is judged by where the leader took it: in the
                # shipping mode the map changes some frames after the step,
                # so let a requested change finish on this copy.
                for _ in range(40):
                    if not m.b(0x7E0117) & 0x80 or run.map_id() != origin:
                        break
                    m.probe(5)
                here = run.map_id()
                if here == origin:
                    px, py = run.me()
                key = int(px) // 8, int(py) // 8, here
                if key in seen:
                    continue
                seen.add(key)
                path = paths[slot] + [buttons]
                reached = (here == destination and distance(px, py) < 80) if destination else distance(px, py) < 10
                if reached:
                    winner = path
                    break
                if here != origin:
                    continue
                if len(saved) >= limit:
                    break
                slot_id = len(saved) + 1
                m.mark(slot_id)
                saved.append(slot_id)
                paths[slot_id] = path
                heapq.heappush(queue, (distance(px, py) / 24 + len(path) * .05, next(serial), slot_id))
            if winner is not None:
                break
    finally:
        m.restore(3999)
        for slot_id in saved:
            m.free(slot_id)
        m.free(3999)
    assert winner is not None, "a path to %s at (%d, %d)" % (destination or "the landmark", x, y)
    for buttons in winner:
        m.run(step, buttons)
    run.say("walk", (x, y), destination, "searched", len(saved), "steps", len(winner), "frame", m.frame)


def arrive(run, x, y, destination):
    walk_to(run, x, y, destination)
    assert run.wait_control(), "the next field in hand"
    assert run.map_id() == destination, "the requested cave or ledge"


def spawn_policy(bot, state, rng, inventory):
    """Hit only the head (slot 9); the shell counters attacks against it."""
    member = state.member
    who = policies.party_ids(bot)[member]
    techs = policies.CHARACTER_TECHS.get(who, {})
    action = policies.survival(state, rng, inventory, member, range(3),
                               cure=techs.get("heal_one"), life=techs.get("revive"))
    if action:
        return action
    if who == 3 and state.mp(member) < 12:
        for item in (policies.MID_ETHER, policies.FULL_ETHER, policies.ETHER):
            action = policies.use(inventory, item, member)
            if action:
                return action
    # Ice, Fire, Uzzi Punch / Rocket Punch: single-target attacks.
    for index, cost in {1: [(2, 2)], 2: [(2, 2)], 3: [(5, 12), (0, 1)]}.get(who, []):
        if state.mp(member) >= cost and policies.tech_known(bot, member, index):
            return "tech", index, 9
    return "attack", 9


def spawn(run, bit, seed):
    m = run.machine
    assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 128, 4000), "the Spawn's command menu"
    run.fight(spawn_policy, lambda r, state: state.hp(9) == 0, seed=seed, max_frames=15000)
    assert run.wait_control(5000), "the field after the Spawn"
    assert m.b(0x7F0064) & bit, "the Spawn's victory flag"


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
            assert run.select_slot(0)
            m.run(4, "A")
            m.run(500)
            assert run.find_player() is not None
            assert run.map_id() == "f600"
            assert m.b(0x7F007C) & 3 == 3
            assert all(item in m.peek(0x7E2400, 242) for item in (0xD9, 0xE2))
            run.checkpoint("loaded")
        else:
            assert run.find_player() is not None
        if start <= 1:
            m.run(180, "UP+B")
            m.run(12, "LEFT+B")
            m.run(50, "UP+B")
            assert run.wait_control()
            arrive(run, 672, 64, "0801")
            run.checkpoint("cave")
        if start <= 2:
            m.run(55, "UP+B")
            spawn(run, 8, args.seed)
            run.checkpoint("spawn1")
        itinerary = [
            (3, "ledge", 88, 192, "0701"),
            (4, "east_cave", 440, 496, "0801"),
            (5, "east_outside", 416, 464, "f600"),
        ]
        for phase, name, x, y, destination in itinerary:
            if start <= phase:
                arrive(run, x, y, destination)
                run.checkpoint(name)
        if start <= 6:
            walk_to(run, 904, 70)
            m.run(2, "UP")
            m.run(3, "A")
            m.run(90)
            assert m.b(0x7F0058) & 128, "the sparkle opens the middle cave"
            run.checkpoint("opened")
        itinerary = [
            (7, "east_back", 976, 112, "0801"),
            (8, "ledge_back", 440, 208, "0701"),
            (9, "first_back", 48, 496, "0801"),
            (10, "west_back", 144, 480, "f600"),
            (11, "middle", 832, 64, "0801"),
            (12, "spawn2_room", 264, 80, "f700"),
        ]
        for phase, name, x, y, destination in itinerary:
            if start <= phase:
                arrive(run, x, y, destination)
                run.checkpoint(name)
        if start <= 13:
            m.run(20, "UP+B")
            spawn(run, 16, args.seed)
            run.checkpoint("spawn2")
        itinerary = [
            (14, "ridge", 0, 912, "0601"),
            (15, "ridge_crossed", 0, 128, "0401"),
            (16, "lower_slope", 632, 1008, "f600"),
            (17, "last_field", 512, 272, "ef01"),
        ]
        for phase, name, x, y, destination in itinerary:
            if start <= phase:
                arrive(run, x, y, destination)
                run.checkpoint(name)
        if start <= 18:
            from .save_point import press, to_slot
            walk_to(run, 104, 640)
            # The point's script may not test the held button during a short tap.
            m.run(30, "A")
            m.run(160)
            assert m.b(0x7E0067) == 11, "the save point's screen"
            to_slot(m, 0)
            press(m, "A", 60)
            press(m, "A", 90)
            press(m, "B", 90)
            assert run.wait_control(), "the field after saving"
            run.checkpoint("saved")
        assert run.map_id() == "ef01" and m.b(0x7F0064) & 0x38 == 0x18
        # Cartridge progress only, for the next movie's starting save. The
        # route's output stays beside its checkpoints until explicitly kept.
        Path(run.prefix + "_summit.srm").write_bytes(m.peek(0x306000, 8192))
        run.finish("two Death Peak Lavos Spawn beaten and the final save point saved")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
