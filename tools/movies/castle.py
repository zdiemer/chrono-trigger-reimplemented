"""Shared approach to Magus's castle from ctsave06 slot 2.

Slot 3 is too late: story byte $A3 is $7E, including both boss defeat bits.
Slot 2 has $A3 = 0 and story counter $87, outside the castle on the 600 A.D. map.
"""
import heapq

from . import policies
from .campaign import Run

SAVE = "oracle/saves/fantasyanime/ctsave06-magus_castle.srm"
FLAGS = 0x7F00A3
MENU_MEMBER = 0x7E95D5
CONTROL = 0x7E011F
# Door tiles are search aims, never timed walks. The destination map is the
# success condition; walls and the approach to a door are tested on copies.
DOORS = {
    ("a500", "a600"): (8, 29), ("a600", "d501"): (3, 4),
    ("d501", "a900"): (23, 4), ("a900", "d501"): (8, 60),
    ("d501", "a600"): (23, 47), ("a600", "ab00"): (19, 4),
    ("ab00", "7d00"): (35, 30), ("7d00", "ad00"): (7, 4),
    ("ad00", "7d00"): (7, 29), ("7d00", "ab00"): (7, 44),
    ("ab00", "a600"): (35, 43),
}
APPROACH = ["loaded", "outside", "hall", "left", "slash_visit", "left_back",
            "hall_back", "right", "corridor", "flea_visit", "corridor_back",
            "right_back", "ozzie", "ambush", "ready"]


def castle_policy(bot, state, rng, inventory):
    """Physical attacks, Spincut where learned, and the save's Mid Tonics.

    Read learned singles from the party record: the battle's tech list isn't
    populated until it is opened. Slash resists the early elemental spells.
    """
    member = state.member
    ids = policies.party_ids(bot)
    character = ids[member]
    learned = bot.machine.b(0x7E2837 + character)
    living = policies.enemies(state)
    target = min(living, key=lambda i: state.hp(i)) if living else None
    if target is not None and state.hp(target) < 60:
        return "attack", target
    dead = [i for i in range(3) if not state.hp(i)]
    if dead:
        action = policies.use(inventory, policies.REVIVE, dead[0])
        if action:
            return action
    low = min((i for i in range(3) if state.hp(i)), key=lambda i: policies.fraction(state, i))
    if policies.fraction(state, low) < (0.65 if character == 1 else 0.4):
        action = policies.use(inventory, policies.MID_TONIC, low)
        if action:
            return action
        if character == 1 and learned & 2 and state.mp(member) >= 5:
            return "tech", 6, low
    crono = next((i for i, who in enumerate(ids) if who == 0), None)
    if crono is not None and state.hp(crono) and state.mp(crono) < 8 and character == 1:
        action = policies.use(inventory, policies.MID_ETHER, crono) or policies.use(inventory, policies.ETHER, crono)
        if action:
            return action
    if character == 0 and learned & 0x10 and state.mp(member) >= 4:
        return "tech", 3, target
    return "attack", target


class CastleRun(Run):
    def settle(self, max_frames=10000):
        """Advance a scene, fighting a corridor ambush if it opens a menu."""
        m = self.machine
        start = m.frame
        while m.frame - start < max_frames:
            if m.b(CONTROL) and not self.dialog() and self.find_player() is not None:
                return self.wait_control()
            if m.b(MENU_MEMBER) < 3 and not m.b(CONTROL):
                battle(self)
            elif self.dialog():
                m.run(3, "A")
                m.run(37)
            else:
                m.run(10)
        return False

    def search(self, goal, aim, max_nodes=3500):
        """Search copies nearest the aim first; replay only the winning path.

        Check the goal before position deduplication: a scene can take control
        or request a new room without moving the leader to another cell.
        """
        m = self.machine
        here = self.map_id()
        m.mark(1)
        queue = [(0, 1, [])]
        seen = set()
        slots = [1]
        winning = None
        try:
            while queue and len(slots) < max_nodes:
                _, slot, path = heapq.heappop(queue)
                for direction in ("UP", "DOWN", "LEFT", "RIGHT"):
                    m.restore(slot)
                    pad = direction + "+B"
                    m.probe(8, pad)
                    moves = path + [(8, pad)]
                    # Let a requested room change finish on this same copy.
                    for _ in range(60):
                        if not m.b(0x7E0117) & 0x80 or self.map_id() != here:
                            break
                        m.probe(5)
                        moves.append((5, 0))
                    x, y = self.me()
                    if goal(m, x, y):
                        winning = moves
                        break
                    if self.map_id() != here:
                        continue
                    if not m.b(CONTROL) or self.dialog():
                        continue
                    key = (int(x) // 4, int(y) // 4)
                    if key in seen:
                        continue
                    seen.add(key)
                    next_slot = len(slots) + 1
                    m.mark(next_slot)
                    slots.append(next_slot)
                    distance = abs(x - aim[0]) + abs(y - aim[1])
                    heapq.heappush(queue, (distance, next_slot, moves))
                if winning is not None:
                    break
        finally:
            m.restore(1)
            for slot in slots:
                m.free(slot)
        self.say("search copies", len(slots), "aim", aim, "found", winning is not None)
        if winning is not None:
            for frames, pad in winning:
                m.run(frames, pad)
            assert goal(m, *self.me()), "winning path replays to its goal"
        return winning

    def exit_to(self, want, rounds=12):
        """Keep cutscene triggers as search outcomes, even before a window opens.

        The castle's entrance takes control while the party walks into place.
        A position-only search discards that stationary wait and never sees the
        dialogue that eventually opens. Resume the search after the scene.
        """
        for _ in range(rounds):
            assert self.find_player() is not None or self.wait_control(), "control before walking"
            tile = DOORS[(self.map_id(), want)]
            aim = (tile[0] * 16 + 8, tile[1] * 16 + 8)
            moves = None
            if self.machine.b(FLAGS) & 2:
                moves = self.search(lambda m, x, y: m.peek(0x7E0100, 2).hex() == want, aim)
            if moves is None:
                moves = self.search(lambda m, x, y: m.peek(0x7E0100, 2).hex() == want
                                    or not m.b(CONTROL) or m.b(0x7E0127) != 0, aim)
            self.say("castle exit", want, "steps", None if moves is None else len(moves),
                     "map", self.map_id(), "position", self.me(), "frame", self.machine.frame)
            if moves is None:
                return False
            if self.map_id() == want:
                return True
            assert self.settle(), "scene on the way to " + want
            if self.map_id() == want:
                return True
        return False

    def talk(self, index):
        assert self.wait_control()
        m = self.machine
        m.mark(4088)
        try:
            tx, ty = self.object_pos(index)
            for dx, dy, facing in self.SPOTS:
                m.restore(4088)
                aim = tx + dx, ty + dy
                path = self.search(lambda m, x, y: abs(x - aim[0]) <= 4 and abs(y - aim[1]) <= 4,
                                   aim, max_nodes=1000)
                if path is None:
                    continue
                m.run(2, facing)
                m.run(8)
                m.run(3, "A")
                m.run(20)
                if self.dialog() or not m.b(CONTROL):
                    self.say("talk", index, "frame", m.frame)
                    return True
            m.restore(4088)
            return False
        finally:
            m.free(4088)


def battle(run, seed=1):
    """Win one battle, remembering that actual enemies have appeared."""
    seen = []

    def won(r, state):
        if policies.enemies(state):
            seen.append(True)
        return bool(seen) and not policies.enemies(state) and any(state.hp(i) for i in range(3))

    run.fight(castle_policy, won, seed=seed, max_frames=30000)


def approach(run, start, seed):
    m = run.machine
    if start <= 0:
        m.run(1620)
        m.run(5, "START")
        m.run(2420 - m.frame)
        assert run.select_slot(1)
        m.run(4, "A")
        for _ in range(200):
            m.run(10)
            if run.map_id() == "f101":
                break
        m.run(120)
        assert run.map_id() == "f101"
        assert m.b(FLAGS) == 0 and m.b(0x7F0000) == 0x87, "before both castle bosses"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.world_exit(max_nodes=100) and run.map_id() == "a500"
        assert run.wait_control()
        run.checkpoint("outside")
    # Visit both empty wings to reveal the false save point in the hall.
    rooms = [("hall", "a600"), ("left", "d501"), ("slash_visit", "a900"),
             ("left_back", "d501"), ("hall_back", "a600"), ("right", "ab00"),
             ("corridor", "7d00"), ("flea_visit", "ad00"), ("corridor_back", "7d00"),
             ("right_back", "ab00"), ("ozzie", "a600")]
    for index, (phase, location) in enumerate(rooms, 2):
        if start <= index:
            assert run.exit_to(location), phase
            assert run.wait_control(), phase + " in hand"
            run.say("position", run.me(), "castle flags", hex(m.b(FLAGS)))
            run.checkpoint(phase)
    if start <= 13:
        assert m.b(FLAGS) & 0x30 == 0x30, "both wings visited"
        assert run.go(11 * 16 + 8, 9 * 16 + 8), "false save point"
        assert run.dialogs_until(lambda: m.b(MENU_MEMBER) < 0x80, 8000), "Ozzie's ambush"
        run.checkpoint("ambush")
    if start <= 14:
        battle(run, seed)
        assert run.wait_control(8000), "ambush won, field in hand"
        assert m.b(FLAGS) & 2, "Ozzie appeared"
        run.checkpoint("ready")
