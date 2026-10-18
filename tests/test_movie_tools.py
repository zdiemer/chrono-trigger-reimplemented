"""The parts of tools/movies that don't need a ROM.

Buttons and the .ctm encoding, the walking search against a fake machine (a grid
with walls and a door), where talk() aims at a walking object, and the battle
policies against synthetic battle state. The driver itself (dev/ct_drive.c) is
checked against `ct_dev --native off` by hand, as docs/MOVIES.md describes;
nothing here runs the machine.
"""
import os
import random
import tempfile
import unittest

from tools.movies import addresses, bfs, ctdrv, mkmovie, policies
from tools.movies.bot import BattleState
from tools.movies.campaign import Run


class TestButtons(unittest.TestCase):
    def test_pad_of_names(self):
        self.assertEqual(mkmovie.pad_of("A"), 1 << 7)
        self.assertEqual(mkmovie.pad_of("UP+B"), (1 << 11) | (1 << 15))
        self.assertEqual(mkmovie.pad_of(0), 0)
        self.assertEqual(mkmovie.pad_of(""), 0)
        self.assertEqual(mkmovie.pad_of("-"), 0)
        self.assertEqual(mkmovie.pad_of(1 << 12), 1 << 12)

    def test_second_controller(self):
        pad = mkmovie.pad_of("A+P2:SELECT+P2:START")
        self.assertEqual(mkmovie.controllers_of(pad), (1 << 7, (1 << 13) | (1 << 12)))
        self.assertEqual(mkmovie.name_of(pad), "A+P2:SELECT+P2:START")
        self.assertEqual(mkmovie.pads_from_events(mkmovie.parse_movie("4 2 P2:START\n"), 6),
                         [0] * 4 + [(1 << 12) << 16] * 2)

    def test_name_of_is_in_movie_order(self):
        self.assertEqual(mkmovie.name_of(mkmovie.pad_of("UP+B")), "B+UP")
        self.assertEqual(mkmovie.name_of(0), "")
        for name in mkmovie.BUTTONS:
            self.assertEqual(mkmovie.name_of(mkmovie.pad_of(name)), name)


class TestMovieFormat(unittest.TestCase):
    def test_events_from_pads_merges_runs(self):
        pads = [0, 0, 128, 128, 128, 0, 1 << 12]
        self.assertEqual(mkmovie.events_from_pads(pads), [(2, 3, 128), (6, 1, 1 << 12)])

    def test_round_trip(self):
        rng = random.Random(7)
        pads = [rng.choice([0, 0, 0, mkmovie.pad_of("A"), mkmovie.pad_of("UP"), mkmovie.pad_of("UP+B")])
                for _ in range(500)]
        events = mkmovie.events_from_pads(pads)
        self.assertEqual(mkmovie.pads_from_events(events, len(pads)), pads)

    def test_text_round_trip(self):
        pads = [0] * 3 + [mkmovie.pad_of("START")] * 5 + [0] * 2 + [mkmovie.pad_of("A")]
        text = "\n".join("%d %d %s" % (start, count, mkmovie.name_of(pad))
                         for start, count, pad in mkmovie.events_from_pads(pads))
        self.assertEqual(mkmovie.pads_from_events(mkmovie.parse_movie(text), len(pads)), pads)

    def test_parse_movie_skips_comments_and_ors_overlaps(self):
        text = "# a comment\n0 4 UP\n2 4 B\n"
        self.assertEqual(mkmovie.pads_from_events(mkmovie.parse_movie(text), 6),
                         [mkmovie.pad_of("UP")] * 2 + [mkmovie.pad_of("UP+B")] * 2 + [mkmovie.pad_of("B")] * 2)

    def test_write_movie(self):
        import tempfile, pathlib
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "m.ctm"
            mkmovie.write_movie(path, [0, 0, 128, 128], ["# header"])
            text = path.read_text(encoding="utf-8")
            self.assertEqual(text, "# header\n2 2 A\n")
            self.assertEqual(mkmovie.load_movie_pads(path, 4), [0, 0, 128, 128])


class FakeMachine:
    """A grid the search can walk: '#' is a wall, 'D' is a door to another map."""

    def __init__(self, rows, start, step=8, cell=6):
        self.rows = rows
        self.x, self.y = start
        self.map = "0001"
        self.frame = 0
        self.slots = {}
        self.pads = []
        self.probes = 0
        self.step, self.cell = step, cell

    # the interface bfs uses
    def mark(self, slot):
        self.slots[slot] = (self.x, self.y, self.map, self.frame)

    def restore(self, slot):
        self.x, self.y, self.map, self.frame = self.slots[slot]
        return self.frame

    def free(self, slot):
        self.slots.pop(slot, None)

    def probe(self, frames, buttons=0):
        self.probes += 1
        self._move(frames, buttons)

    def run(self, frames, buttons=0):
        self._move(frames, buttons)
        self.pads.append((frames, buttons))

    def _move(self, frames, buttons):
        self.frame += frames
        if self.map != "0001" or not buttons:
            return
        # one cell of movement per `step` frames, walls block
        dx, dy = {"UP": (0, -1), "DOWN": (0, 1), "LEFT": (-1, 0), "RIGHT": (1, 0)}[buttons]
        for _ in range(max(1, frames // self.step)):
            nx, ny = self.x + dx * self.cell, self.y + dy * self.cell
            tile = self._tile(nx, ny)
            if tile == "#":
                break
            self.x, self.y = nx, ny
            if tile == "D":
                self.map = "0002"
                break

    def _tile(self, x, y):
        row = y // self.cell
        column = x // self.cell
        if row < 0 or row >= len(self.rows) or column < 0 or column >= len(self.rows[row]):
            return "#"
        return self.rows[row][column]

    # what the search's pos/map_id callbacks use
    def position(self):
        return (self.x, self.y)

    def map_id(self):
        return self.map


MAZE = [
    "....#....",
    ".##.#.##.",
    ".#..#..#.",
    ".#.###.#.",
    ".#.....#D",
    ".#######.",
    ".........",
]


class TestWalkingSearch(unittest.TestCase):
    def machine(self, start=(0, 0)):
        return FakeMachine(MAZE, (start[0] * 6, start[1] * 6))

    def test_finds_a_path_and_logs_only_that_path(self):
        machine = self.machine()
        goal = (8 * 6, 4 * 6)
        moves, visited = bfs.bfs_walk(machine, lambda m, x, y: (x, y) == goal,
                                      lambda m: m.position(), lambda m: m.map_id())
        self.assertIsNotNone(moves)
        self.assertEqual(machine.position(), goal)
        # the run() log is the path only, not the whole search
        self.assertEqual(len(machine.pads), len(moves))
        self.assertLess(len(moves), machine.probes)
        self.assertTrue(all(pad in ("UP", "DOWN", "LEFT", "RIGHT") for _, pad in machine.pads))

    def test_reaching_another_map_is_a_goal(self):
        machine = self.machine()
        moves, _ = bfs.bfs_walk(machine, lambda m, x, y: m.map_id() == "0002",
                                lambda m: m.position(), lambda m: m.map_id())
        self.assertIsNotNone(moves)
        self.assertEqual(machine.map_id(), "0002")

    def test_unreachable_goal_returns_none_and_restores(self):
        machine = self.machine()
        machine.frame = 1234
        moves, visited = bfs.bfs_walk(machine, lambda m, x, y: (x, y) == (-6, -6),
                                      lambda m: m.position(), lambda m: m.map_id())
        self.assertIsNone(moves)
        self.assertEqual(machine.position(), (0, 0))
        self.assertEqual(machine.frame, 1234)
        self.assertEqual(machine.pads, [])
        self.assertTrue(visited)

    def test_cell_size_sets_the_step(self):
        machine = FakeMachine(MAZE, (0, 0), step=8, cell=6)
        bfs.bfs_walk(machine, lambda m, x, y: (x, y) == (0, 6 * 6), lambda m: m.position(),
                     lambda m: m.map_id(), step=8, cell=6)
        self.assertEqual([pad for _, pad in machine.pads], ["DOWN"] * 6)
        self.assertEqual({frames for frames, _ in machine.pads}, {8})

    def test_slots_are_freed(self):
        machine = self.machine()
        bfs.bfs_walk(machine, lambda m, x, y: False, lambda m: m.position(), lambda m: m.map_id())
        self.assertEqual(machine.slots, {})


class PacingRoom:
    """Enough of a Run for object_path() and aims(): a party standing still and an
    object whose position follows the machine's clock, up and down between two
    ends (the Dream Team room's pair; docs/MOVIES.md, "A race in the route")."""

    def __init__(self, party, near, far, period=240):
        self.party, self.near, self.far, self.period = party, near, far, period
        self.machine = self
        self.frame = 0
        self.saved = None

    # the Machine side
    def mark(self, slot):
        self.saved = self.frame

    def restore(self, slot):
        self.frame = self.saved
        return self.frame

    def free(self, slot):
        self.saved = None

    def probe(self, frames, buttons=0):
        self.frame += frames

    # the Run side
    def me(self):
        return self.party

    def object_pos(self, index):
        half = self.period / 2
        phase = self.frame % self.period
        part = phase / half if phase < half else (self.period - phase) / half
        return (88.0, self.near + (self.far - self.near) * part)


class TestTalkAims(unittest.TestCase):
    def test_object_path_walks_a_round_and_puts_the_machine_back(self):
        room = PacingRoom(party=(88.0, 160.0), near=136.0, far=88.0)
        room.frame = 5000
        path = Run.object_path(room, 12)
        self.assertEqual(room.frame, 5000)
        self.assertIsNone(room.saved)
        self.assertAlmostEqual(min(y for _, y in path), 88.0, delta=2)      # sampled, so near the ends
        self.assertAlmostEqual(max(y for _, y in path), 136.0, delta=2)

    def test_aims_puts_the_end_nearest_the_party_first(self):
        room = PacingRoom(party=(88.0, 160.0), near=136.0, far=88.0)
        room.frame = 120                                    # the object at the far end
        path = Run.object_path(room, 12)
        aims = Run.aims(room, 12, path)
        self.assertAlmostEqual(aims[0][1], 136.0, places=1)         # the near end
        self.assertEqual(aims[1], room.object_pos(12))              # then where it stands
        self.assertAlmostEqual(aims[1][1], 88.0, places=1)

    def test_a_still_object_gives_one_aim(self):
        room = PacingRoom(party=(88.0, 160.0), near=120.0, far=120.0)
        self.assertEqual(Run.aims(room, 12, Run.object_path(room, 12)), [(88.0, 120.0)])


class FakeMemory:
    """Enough of a Machine for the policies: the tech list and the party."""

    def __init__(self, party=(0, 1, 0x80), techs=None):
        self.party = bytes(party)
        self.techs = techs or {}

    def b(self, addr):
        if addresses.BATTLE_TECH_LIST_BASE <= addr < addresses.BATTLE_TECH_LIST_BASE + 3:
            return (addr - addresses.BATTLE_TECH_LIST_BASE) * 0x14      # the real table's stride
        if addresses.BATTLE_TECH_LIST <= addr < addresses.BATTLE_TECH_LIST + 0x40:
            return self.techs.get(addr - addresses.BATTLE_TECH_LIST, 0)
        raise AssertionError("unexpected read of %06X" % addr)

    def peek(self, addr, length):
        if addr == addresses.PARTY_MEMBERS:
            return self.party[:length]
        raise AssertionError("unexpected read of %06X" % addr)


class FakeBot:
    def __init__(self, party=2, memory=None):
        self.party = party
        self.machine = memory or FakeMemory()


def battle_state(member=0, party_hp=((500, 500, 40, 40), (400, 400, 40, 40), (0, 0, 0, 0)),
                 enemies=((3, 1000, 1000),)):
    """Pack synthetic stats the way Bot reads them."""
    data = bytearray(20 + 8 * 11)
    data[0] = member
    data[14:20] = bytes([0xFF] * 6)
    for slot, (hp, max_hp, mp, max_mp) in enumerate(party_hp):
        data[20 + 8 * slot: 28 + 8 * slot] = bytes([hp & 0xFF, hp >> 8, max_hp & 0xFF, max_hp >> 8,
                                                    mp & 0xFF, mp >> 8, max_mp & 0xFF, max_mp >> 8])
    for slot, hp, max_hp in enemies:
        data[20 + 8 * slot: 24 + 8 * slot] = bytes([hp & 0xFF, hp >> 8, max_hp & 0xFF, max_hp >> 8])
    return BattleState(0, bytes(data))


def inventory(counts):
    """(ids, counts) for an inventory holding the given {item: count}."""
    ids = bytearray(addresses.INVENTORY_SIZE)
    amounts = bytearray(addresses.INVENTORY_SIZE)
    for index, item in enumerate(sorted(counts)):
        ids[index] = item
        amounts[index] = counts[item]
    return (bytes(ids), bytes(amounts))


class TestPolicies(unittest.TestCase):
    def test_item_index_skips_items_the_battle_menu_hides(self):
        items = inventory({policies.TONIC: 1, policies.REVIVE: 2, 0xC8: 3})    # $C8 Shelter: not in battle
        self.assertEqual(policies.item_index(items, policies.TONIC), 0)
        self.assertEqual(policies.item_index(items, policies.REVIVE), 1)
        self.assertIsNone(policies.item_index(items, policies.MEGA_ELIXIR))

    def test_item_index_skips_empty_stacks(self):
        items = inventory({policies.TONIC: 0, policies.REVIVE: 1})
        self.assertEqual(policies.item_index(items, policies.REVIVE), 0)

    def test_enemies_ignores_dummies_and_the_dead(self):
        state = battle_state(enemies=((3, 100, 100), (4, 1, 1), (5, 0, 900)))
        self.assertEqual(policies.enemies(state), [3])

    def test_survival_revives_a_dead_ally(self):
        state = battle_state(member=1, party_hp=((0, 500, 40, 40), (400, 400, 40, 40), (0, 0, 0, 0)))
        items = inventory({policies.REVIVE: 3})
        action = policies.survival(state, random.Random(1), items, 1, (0, 1), life=None)
        self.assertEqual(action, ("item", 0, 0))

    def test_survival_revives_with_a_tech_when_it_has_the_mp(self):
        state = battle_state(member=1, party_hp=((0, 500, 40, 40), (400, 400, 40, 40), (0, 0, 0, 0)))
        action = policies.survival(state, random.Random(1), inventory({}), 1, (0, 1), life=(7, 15))
        self.assertEqual(action, ("tech", 7, 0))

    def test_survival_leaves_a_healthy_party_alone(self):
        state = battle_state()
        items = inventory({policies.FULL_TONIC: 9, policies.MEGA_ELIXIR: 9, policies.REVIVE: 9})
        for seed in range(20):
            self.assertIsNone(policies.survival(state, random.Random(seed), items, 0, (0, 1)))

    def test_survival_heals_when_everyone_is_low(self):
        state = battle_state(party_hp=((80, 500, 40, 40), (60, 400, 40, 40), (0, 0, 0, 0)))
        items = inventory({policies.FULL_TONIC: 9, policies.MEGA_ELIXIR: 9})
        actions = {policies.survival(state, random.Random(seed), items, 0, (0, 1)) for seed in range(20)}
        self.assertTrue(actions)
        self.assertTrue(all(action is not None and action[0] == "item" for action in actions))

    def test_crono_and_marle_casts_luminaire_with_mp(self):
        state = battle_state(member=0, party_hp=((500, 500, 40, 40), (400, 400, 40, 40), (0, 0, 0, 0)))
        items = inventory({policies.FULL_TONIC: 9})
        action = policies.crono_and_marle(FakeBot(), state, random.Random(3), items)
        self.assertEqual(action, ("tech", 7, None))

    def test_crono_and_marle_attacks_without_mp(self):
        state = battle_state(member=0, party_hp=((500, 500, 0, 40), (400, 400, 40, 40), (0, 0, 0, 0)))
        actions = {policies.crono_and_marle(FakeBot(), state, random.Random(seed), inventory({}))
                   for seed in range(10)}
        self.assertEqual(actions, {("attack", 3)})

    def test_core_policy_breaks_the_body_before_the_core(self):
        state = battle_state(member=0, party_hp=((900, 900, 0, 40), (800, 800, 40, 40), (0, 0, 0, 0)),
                             enemies=((3, 10000, 10000), (7, 2000, 2000), (9, 30000, 30000)))
        targets = {policies.crono_and_marle_core(FakeBot(), state, random.Random(seed), inventory({}))[1]
                   for seed in range(20)}
        self.assertTrue(targets <= {3, 7}, targets)

    def test_core_policy_hits_the_core_once_it_is_open(self):
        state = battle_state(member=0, party_hp=((900, 900, 0, 40), (800, 800, 40, 40), (0, 0, 0, 0)),
                             enemies=((3, 0, 10000), (7, 0, 2000), (9, 30000, 30000)))
        action = policies.crono_and_marle_core(FakeBot(), state, random.Random(5), inventory({}))
        self.assertEqual(action, ("attack", 9))

    def test_party_policy_uses_the_party_member_ids(self):
        # Robo (id 3) in slot 1 heals the party with Heal Beam (tech 4, 3 MP).
        memory = FakeMemory(party=(0, 3, 2), techs={0x14 + 4: 0x00})
        state = battle_state(member=1, party_hp=((100, 900, 40, 40), (120, 800, 40, 40), (90, 700, 40, 40)),
                             enemies=((3, 500, 500),))
        bot = FakeBot(party=3, memory=memory)
        actions = {policies.party(bot, state, random.Random(seed), inventory({})) for seed in range(30)}
        self.assertIn(("tech", 4, None), actions)

    def test_party_policy_skips_techs_that_are_blank_rows(self):
        memory = FakeMemory(party=(0, 3, 2), techs={0x14 + 4: 0xFF, 0x14 + 1: 0xFF, 0x14 + 6: 0xFF,
                                                    0x14 + 2: 0xFF, 0x14 + 5: 0xFF, 0x14 + 0: 0xFF})
        state = battle_state(member=1, party_hp=((100, 900, 40, 40), (120, 800, 40, 40), (90, 700, 40, 40)),
                             enemies=((3, 500, 500),))
        bot = FakeBot(party=3, memory=memory)
        actions = {policies.party(bot, state, random.Random(seed), inventory({})) for seed in range(30)}
        self.assertTrue(all(action[0] == "attack" for action in actions), actions)


class TestBattleState(unittest.TestCase):
    def test_menu_open_needs_the_command_page(self):
        state = battle_state(member=0)
        self.assertTrue(state.menu_open())
        state.page = 1
        self.assertFalse(state.menu_open())
        state.page, state.targeting = 0, 1
        self.assertFalse(state.menu_open())
        state.targeting, state.member = 0, 0xFF
        self.assertFalse(state.menu_open())

    def test_stats_are_four_words_per_slot(self):
        state = battle_state(party_hp=((123, 456, 7, 8), (0, 0, 0, 0), (0, 0, 0, 0)))
        self.assertEqual((state.hp(0), state.max_hp(0), state.mp(0)), (123, 456, 7))


class TestAddresses(unittest.TestCase):
    def test_all_addresses_are_documented_bus_addresses(self):
        import tools.movies.addresses as module
        for name, value in vars(module).items():
            if name.isupper() and isinstance(value, int) and name != "INVENTORY_SIZE" \
                    and name != "BATTLE_STAT_STRIDE":
                self.assertTrue(0x7E0000 <= value <= 0x7FFFFF or 0xC00000 <= value <= 0xFFFFFF,
                                "%s = %06X is not a work RAM or ROM address" % (name, value))


class TestKeptInput(unittest.TestCase):
    """The input a Machine keeps is what the route count replays, so frames
    with no buttons kept for them are errors, not frames of no buttons."""

    def machine(self):
        m = ctdrv.Machine.__new__(ctdrv.Machine)
        m.frame, m.pads, m.counted = 0, [], None
        m.answers = []

        def cmd(line):
            m.answers.append(line)
            verb, *args = line.split()
            if verb in ("run", "runw"):
                return "%d" % (m.frame + int(args[0]))
            return "%d" % m.loaded
        m.cmd = cmd
        return m

    def test_a_probe_not_restored_stops_the_next_run(self):
        m = self.machine()
        m.run(3, "A")
        m.probe(2, "B")
        with self.assertRaises(ctdrv.DriveError):
            m.run(1)
        with self.assertRaises(ctdrv.DriveError):
            m.runw(1)
        self.assertEqual(m.pads, [mkmovie.pad_of("A")] * 3)

    def test_a_state_needs_its_input(self):
        m = self.machine()
        m.run(3, "A")
        with tempfile.TemporaryDirectory() as scratch:
            state = os.path.join(scratch, "s.state")
            m.loaded = 2
            self.assertEqual(m.load_state(state), 2)
            self.assertEqual(m.pads, [])
            with self.assertRaises(ctdrv.DriveError):
                m.run(1)
            with open(state + ".pads", "w", encoding="utf-8") as handle:
                handle.write("1 2")
            self.assertEqual(m.load_state(state), 2)
            self.assertEqual(m.pads, [1, 2])
            m.loaded = 3
            with self.assertRaises(ctdrv.DriveError):
                m.load_state(state)


if __name__ == "__main__":
    unittest.main()
