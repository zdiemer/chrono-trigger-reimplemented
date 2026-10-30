"""Travel to Geno Dome from ctsave14 slot 2 and confront MotherBrain."""
import heapq
from .. import addresses as A
from .. import bfs, policies
from ..campaign import Run, route_arguments, run_route
from .epoch_lavos import wait_until, DIAL_PERIOD
from .menu_sweep import press

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
PHASES = ["loaded", "boarded", "future", "entrance", "robo", "console",
          "conveyor_entry", "conveyor_won", "main_floor", "code_open", "doll2",
          "upper", "far_lift", "robot_room_open", "robot_following", "dolls",
          "atropos_won", "boss_door", "motherbrain_won", "finished"]
HEADER = """\
# Geno Dome, 2300 A.D. (route motherbrain).
# Epoch from 600 A.D.; conveyor, charged pods, dolls and escort; Atropos and MotherBrain.
# Ends with control in Geno Dome's entrance after the shutdown and return to the world map.
# Save: ctsave14 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def fight_once(run, party=3, seed=1, policy="party"):
    seen = []

    def won(r, state):
        if policies.enemies(state):
            seen.append(True)
        return bool(seen) and not policies.enemies(state) and any(state.hp(i) for i in range(party))

    run.fight(policy, won, party=party, seed=seed, max_tries=100)


def seek(run, x, y, goal=None, battle_var=11, max_nodes=3800):
    """Search machine copies toward a spot, dashing and stopping for encounters.

    Prioritize distance to the destination on these large floors. Test event
    flags before discarding a repeated position: touching a charging pod can
    change its flag without moving into another position cell.
    """
    m = run.machine
    here = run.map_id()
    goal = goal or (lambda: abs(run.me()[0] - x) < 8 and abs(run.me()[1] - y) < 8)
    interrupted = lambda: m.b(0x7F0200 + 2 * battle_var) == 1 or run.dialog()
    m.mark(4091)
    queue = [(0, 0, 4091, [])]
    visited = set()
    slots = []
    result = None
    try:
        while queue and len(slots) < max_nodes:
            _, cost, slot, path = heapq.heappop(queue)
            for direction in bfs.DIRECTIONS:
                m.restore(slot)
                m.probe(8, direction + "+B")
                px, py = run.me()
                if goal() or interrupted():
                    result = path + [direction]
                    break
                if run.map_id() != here:
                    continue
                key = (int(px) // 6, int(py) // 6)
                if key in visited:
                    continue
                visited.add(key)
                child = len(slots) + 1
                m.mark(child)
                slots.append(child)
                priority = cost + 1 + (abs(px - x) + abs(py - y)) / 12
                heapq.heappush(queue, (priority, cost + 1, child, path + [direction]))
            if result is not None:
                break
    finally:
        for slot in slots:
            m.free(slot)
        m.restore(4091)
        m.free(4091)
    if result is not None:
        for direction in result:
            m.run(8, direction + "+B")
    run.say("seek", (x, y), "steps", result and len(result), "searched", len(slots), "at", run.me())
    return result is not None


def walk_to(run, x, y, goal=None, battle_var=11, fight=None):
    """Walk a floor, resolving the encounters that take control along the way
    (with `fight`, given the run, where an encounter needs other than the
    whole party's fight)."""
    m = run.machine
    for _ in range(12):
        assert run.wait_control(), "field control before walking"
        reached = goal or (lambda: abs(run.me()[0] - x) < 8 and abs(run.me()[1] - y) < 8)
        if reached():
            return
        assert seek(run, x, y, reached, battle_var), "walk to %s" % ((x, y),)
        if reached():
            return
        if run.dialog() and not m.b(0x7F0200 + 2 * battle_var):
            assert run.wait_control(), "floor speech finishes"
            continue
        assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 6000), "encounter starts"
        (fight or fight_once)(run)
    raise AssertionError("too many encounters on the walk")


def face_and_confirm(m):
    press(m, "UP", 8, 2)
    press(m, "A", 80)


def take_lift(run, destination):
    assert run.wait_control()
    assert run.talk(8, lambda: run.map_id() == destination), "elevator to " + destination
    assert run.wait_control()


def escort_robot(run):
    """Keep object 8 around the corridor corners and across the reversed belt.

    The escort follows the leader directly, colliding with walls and the other
    party members. A shortest dash leaves it behind. Down alone balances the
    belt's upward motion while the robot catches up; Down+B advances.
    """
    m = run.machine
    assert seek(run, 344, 320)
    m.run(240)
    m.run(8, "DOWN+B")
    m.run(240)
    for _ in range(21):
        m.run(8, "RIGHT")
        m.run(30)
    m.run(56, "LEFT+B")
    m.run(12, "UP+B")
    m.run(100)
    for _ in range(14):
        m.run(8, "RIGHT")
        m.run(30)
    m.run(24, "DOWN")
    m.run(32, "LEFT")
    m.run(50)
    m.run(72, "RIGHT")
    for _ in range(100):
        m.run(60, "DOWN")
        if run.object_pos(8)[0] >= 528:
            break
    assert run.object_pos(8)[0] >= 528, "escort reaches the belt"
    for _ in range(10):
        m.run(20, "DOWN+B")
        for _ in range(100):
            m.run(30, "DOWN")
            p, q = run.me(), run.object_pos(8)
            if max(abs(p[0] - q[0]), abs(p[1] - q[1])) <= 32:
                break
    m.run(24, "DOWN+B")
    m.run(24, "RIGHT+B")
    m.run(1000)
    for direction, count in [("DOWN", 20), ("RIGHT", 86), ("DOWN", 132),
                             ("LEFT", 32), ("DOWN", 20), ("LEFT", 500),
                             ("UP", 112), ("LEFT", 32)]:
        for step in range(0, count, 4):
            m.run(min(4, count - step), direction)
            for _ in range(100):
                p, q = run.me(), run.object_pos(8)
                # The follower may stop within 32 pixels on both axes.
                # A Manhattan-distance threshold can wait forever there.
                if max(abs(p[0] - q[0]), abs(p[1] - q[1])) <= 32:
                    break
                m.run(30)
        m.run(200)
        run.say("escort", direction, "leader", run.me(), "robot", run.object_pos(8))
    m.run(25, "UP")
    m.run(500)
    assert m.b(0x7F013C) & 0x80, "escort blocks the guard"


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(1)
            m.run(4, "A")
            assert wait_until(m, lambda: run.map_id() == "f101", 2500)
            m.run(120)
            run.say("world", run.world_pos(), "Epoch", m.w(0x7E0290), m.w(0x7E0292))
            run.checkpoint("loaded")
        if start <= 1:
            moves, _ = bfs.ow_bfs(m, lambda m, x, y: m.b(A.OVERWORLD_TRAVEL) != 0,
                                  lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(),
                                  confirm=True, max_nodes=800)
            assert moves is not None, "board the Epoch"
            assert wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 2, 1000)
            m.run(60)
            run.checkpoint("boarded")
        if start <= 2:
            m.run(4, "Y")
            m.run(100)
            for _ in range(7):
                if m.b(DIAL_PERIOD) == 1:
                    break
                m.run(4, "UP")
                m.run(56)
            assert m.b(DIAL_PERIOD) == 1, "2300 A.D. on the dial"
            m.run(4, "A")
            assert wait_until(m, lambda: run.map_id() == "f201", 5000)
            m.run(1000)
            run.say("future", run.world_pos(), "travel", m.b(A.OVERWORLD_TRAVEL))
            run.checkpoint("future")
        if start <= 3:
            # The 2300 A.D. exit table puts Geno Dome at (72, 46).
            # The open ground southwest of it lets the Epoch land.
            for axis, target, lower, higher in [(0, 1128, "LEFT", "RIGHT"),
                                                (1, 768, "UP", "DOWN")]:
                for _ in range(200):
                    delta = target - run.world_pos()[axis]
                    if abs(delta) < 8:
                        break
                    m.run(4, higher if delta > 0 else lower)
                m.run(20)
            m.run(3, "A")
            assert wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 0, 1200), "land near Geno Dome"
            m.run(120)
            assert run.world_exit(max_nodes=120) and run.map_id() == "f800", "Geno Dome"
            assert run.wait_control()
            run.checkpoint("entrance")
        if start <= 4:
            # The party exchange has Robo in the reserve's second row on this save.
            press(m, "Y", 100)
            assert m.b(0x7E0067) == 7, "party exchange"
            for button in ("A", "RIGHT", "DOWN", "A"):
                press(m, button, 50)
            press(m, "B", 120)
            assert m.b(A.PARTY_MEMBERS) == 3, "Robo leading"
            assert run.wait_control()
            run.checkpoint("robo")
        if start <= 5:
            assert run.talk(8, lambda: bool(m.b(0x7F013C) & 1)), "Robo opens the console"
            run.checkpoint("console")
        if start <= 6:
            assert run.exit_to("f900"), "conveyor approach"
            run.checkpoint("conveyor_entry")
        if start <= 7:
            assert run.wait_control()
            assert run.exit_to("7e00"), "conveyor"
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 6000)
            run.fight("party", lambda r, s: r.map_id() == "fa00", seed=args.seed,
                      max_frames=100000, max_tries=100)
            assert run.wait_control()
            run.checkpoint("conveyor_won")
        if start <= 8:
            assert run.talk(1, lambda: bool(m.b(0x7F020C) & 1)), "open the door beyond the conveyor"
            assert run.exit_to("0001"), "Geno Dome main floor"
            assert run.wait_control()
            run.checkpoint("main_floor")
        if start <= 9:
            walk_to(run, 376, 450)
            press(m, "UP", 8, 2)
            press(m, "A", 80)
            assert m.b(0x7F013D) & 0x80, "charging chamber open"
            walk_to(run, 184, 114)
            press(m, "UP", 8, 2)
            press(m, "A", 80)
            walk_to(run, 248, 114)
            press(m, "UP", 8, 2)
            press(m, "A", 80)
            assert m.b(0x7F013D) & 0x70 == 0x50, "first and third switches on, middle off"
            run.checkpoint("code_open")
        if start <= 10:
            walk_to(run, 400, 415, lambda: bool(m.b(0x7F013C) & 0x40))
            assert seek(run, 144, 216, lambda: bool(m.b(0x7F013E) & 4)), "charge the doll's pod"
            assert m.b(0x7F013E) & 4
            m.run(80)
            assert seek(run, 88, 114), "the doll's room"
            press(m, "UP", 8, 2)
            press(m, "A", 80)
            assert m.b(0x7F013C) & 4, "second doll collected"
            run.checkpoint("doll2")
        if start <= 11:
            walk_to(run, 632, 690)
            face_and_confirm(m)
            m.run(40, "UP")
            m.run(120)
            assert run.map_id() == "7f00", "near elevator"
            take_lift(run, "0c01")
            run.checkpoint("upper")
        if start <= 12:
            walk_to(run, 616, 512, lambda: run.map_id() == "8000", battle_var=9)
            m.run(100)
            assert run.wait_control()
            # Object 0 interrupts the corridor at row 15 with a one-time speech.
            for _ in range(300):
                if run.map_id() == "0b01":
                    break
                if run.dialog():
                    press(m, "A", 37)
                else:
                    m.run(20, "UP+B")
            assert run.map_id() == "0b01", "far upper floor"
            assert run.wait_control()
            if not m.b(0x7F013F) & 0x20:
                assert seek(run, 888, 520, lambda: m.b(0x7E011F) == 0, battle_var=9)
                assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 6000)
                fight_once(run)
                assert run.wait_control()
            assert seek(run, 888, 525)
            face_and_confirm(m)
            m.run(40, "UP")
            m.run(120)
            assert run.map_id() == "7f00", "far elevator"
            run.checkpoint("far_lift")
        if start <= 13:
            take_lift(run, "0001")
            walk_to(run, 648, 306)
            face_and_confirm(m)
            assert m.b(0x7F013D) & 8, "nearby door open"
            walk_to(run, 520, 178)
            face_and_confirm(m)
            assert m.b(0x7F013D) & 4, "conveyor changed"
            walk_to(run, 424, 306)
            face_and_confirm(m)
            assert m.b(0x7F013D) & 2, "escort pod approach open"
            run.checkpoint("robot_room_open")
        if start <= 14:
            # Clear both lower corridors before the timed charge and escort.
            # The left encounter is positional; the right one starts by
            # touching the patrol (object $24, moving around tiles 39..43).
            if not m.b(0x7F013F) & 1:
                walk_to(run, 120, 714)
                assert m.b(0x7F013F) & 1, "escort corridor battle cleared"
            if not m.b(0x7F013F) & 2:
                walk_to(run, 680, 720, lambda: bool(m.b(0x7F013F) & 2))
            assert m.b(0x7F013F) & 0x1F == 0x1F, "main floor encounters cleared"
            walk_to(run, 400, 415, lambda: bool(m.b(0x7F013C) & 0x40))
            assert seek(run, 400, 264, lambda: bool(m.b(0x7F013E) & 2)), "charge the escort pod"
            assert m.b(0x7F013E) & 2
            m.run(80)
            walk_to(run, 344, 136, lambda: m.b(0x7F0218) == 1)
            run.checkpoint("robot_following")
        if start <= 15:
            run.find_player()
            escort_robot(run)
            assert seek(run, 120, 530)
            face_and_confirm(m)
            assert m.b(0x7F013C) & 6 == 6, "both dolls collected"
            run.checkpoint("dolls")
        if start <= 16:
            walk_to(run, 632, 690)
            face_and_confirm(m)
            m.run(40, "UP")
            m.run(120)
            assert run.map_id() == "7f00"
            take_lift(run, "0c01")
            # A patrol on the upper floor may still be alive: the whole party
            # fights it. Walk on until Atropos's scene interrupts the walk and
            # Robo, the party split for it, fights alone. The walk's goal is
            # the fight won, not a place: stopped as the party steps inside
            # the trigger's rectangle, mid-step, the scene can stand still
            # with the party never split (the shipping mode's timing left
            # the party at (525, 699) after the patrol and showed it).
            def solo():
                return m.b(A.PARTY_MEMBERS) == 3 and all(
                    m.b(A.PARTY_MEMBERS + i) >= 0x80 for i in (1, 2))

            walk_to(run, 504, 696, lambda: bool(m.b(0x7F013B) & 0x20), battle_var=9,
                    fight=lambda r: fight_once(r, party=1 if solo() else 3))
            assert run.wait_control(12000)
            assert m.b(0x7F013B) & 0x20, "Atropos defeated"
            run.checkpoint("atropos_won")
        if start <= 17:
            walk_to(run, 520, 690, battle_var=9)
            face_and_confirm(m)
            assert run.wait_control()
            assert m.b(0x7F013E) & 8, "upper door open"
            if m.b(0x7F014E) & 0x18 == 8:
                # The one-time speech takes control at tile x < 18, then
                # delays before opening its window. Stop on its story bit.
                walk_to(run, 320, 712, battle_var=9)
                walk_to(run, 312, 616, battle_var=9)
                assert seek(run, 280, 616, lambda: bool(m.b(0x7F014E) & 0x10), battle_var=9)
                assert run.wait_control()
            walk_to(run, 72, 370, battle_var=9)
            face_and_confirm(m)
            assert m.b(0x7F013C) & 0x10, "first doll placed"
            walk_to(run, 248, 370, battle_var=9)
            face_and_confirm(m)
            assert m.b(0x7F013C) & 0x20, "second doll placed"
            run.checkpoint("boss_door")
        if start <= 18:
            assert run.wait_control()
            assert seek(run, 160, 200, lambda: m.b(0x7E011F) == 0, battle_var=9)
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 8000)
            fight_once(run, policy="motherbrain", seed=args.seed)
            assert run.dialogs_until(lambda: run.map_id() == "f201", 12000)
            m.run(600)
            assert m.b(0x7F013B) & 0x10, "MotherBrain defeated"
            assert m.b(A.OVERWORLD_TRAVEL) == 0, "back on foot"
            run.checkpoint("motherbrain_won")
        if start <= 19:
            assert run.world_exit(max_nodes=120) and run.map_id() == "f800"
            assert run.wait_control()
            m.run(120)
            run.checkpoint("finished")
        assert run.map_id() == "f800" and m.b(0x7F013B) & 0x10
        m.screenshot(run.prefix + "_end.png")
        run.finish("MotherBrain defeated; control in Geno Dome's entrance")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if m.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
