"""Leg B of the playthrough: 65,000,000 B.C. to Tyrano Lair.

Save: ctsave05 slot 1 (fantasyanime archive; Crono, Ayla and Marle on the 65,000,000 B.C.
world map before the Forest Maze, story $78).

One of the branched playthrough's legs: the main playthrough (route playthrough) plays the
early game from a blank cartridge, and each leg starts from a cartridge save so that the whole
game is played in parallel. This one goes through the Forest Maze to the Reptite Lair and
Nizbel (its piece, route nizbel); back through the Mystic Mountains' Gate to the End of Time;
to Melchior's hut in 1000 A.D., where Melchior and Lucca forge the Masamune; through Medina's
Gate and Truce Canyon's to 600 A.D., over Zenan Bridge to the Cursed Woods, where Frog takes
the Masamune and joins; to the Magic Cave's mountainside, where Frog remembers (route
magic_cave's piece) and opens the cave, and through it to Magus's castle (the castle routes'
approach, tools/movies/castle.py), its generals, Ozzie and the stair down to Magus's chamber.
The party and items are the save's, with Mid Tonics and Revives bought at Dorino. Each phase ends at a
checkpoint, and the route goes on only past checkpoints that succeeded.
"""
from .. import addresses as A
from .. import castle
from .. import policies
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import toward, world_enter
from . import _trial_party as early
from . import castle_flea
from . import castle_slash
from . import magic_cave
from . import menu_sweep as menus
from . import nizbel
from . import playthrough as main_leg

SAVE = "saves/fantasyanime/ctsave05-prehistoric.srm"
SLOT = 0
HEADER = """\
# Leg B of the playthrough (route playthrough_leg-b): 65,000,000 B.C. to Tyrano Lair.
# Save: ctsave05 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

STORY = A.EVENT_MEMORY
WORLD_1000, WORLD_600, WORLD_65M = "f001", "f101", "f301"
END_OF_TIME = "d001"

# 65,000,000 B.C.
FOREST_MAZE = 0x11A
STORY_BEFORE_MAZE = 0x78
# The maze's way out at its foot (tile 6-8, 47), onto the world map by the Reptite Lair.
MAZE_FOOT = (7 * 16 + 8, 47 * 16 + 8)
MAZE_SEARCH = 8000          # places the walk may try: the maze's dead ends lie between its head and foot
BELOW_MAZE = 740            # world-map pixel row: below the maze, by the lair (above it, back by its head)
MYSTIC_MOUNTAINS = 0x112    # its foot; the way up goes on through $111 to $110, where the Gate is
MOUNTAIN_PATH, MOUNTAIN_TOP = "1101", "1001"
MYSTIC_GATE = (168, 180)    # below the Gate's objects (8, 9); stepping on starts Ayla's farewell and the Gate
STORY_GATE_KEY_HOME = 0x7E

# The End of Time's pillars of light, where the party stands to take each (a pixel off its middle
# and the pillar doesn't answer the confirm button).
PILLAR_MEDINA, PILLAR_600 = (232, 139), (200, 119)
MEDINA_HOUSE, TRUCE_CANYON_600, TRUCE_CANYON_600_FOOT = "2400", "7100", "7000"

# 1000 A.D.
MELCHIORS_HUT, HUT_BELOW = 0x028, "2900"
MELCHIOR = 8                # in his hut; spoken to, he takes the Masamune's pieces and the Dreamstone downstairs
STORY_MASAMUNE = 0x81
MEDINA_HOUSE_LOCATION = 0x024
MEDINA_CUPBOARD = (112, 104)    # below the cupboard the Gate is in, in the Medina house the pillar leads to

# 600 A.D.
ZENAN_BRIDGE, ZENAN_NORTH_ROW = 0x087, 0x1C
ZENAN_WEST = (8, 7 * 16 + 8)    # the bridge's south-west end, tile 0, 7
DORINO_SHOP, DORINO_COUNTER = 0x08B, (896, 424)   # below the merchant
DORINO_MID_TONIC_ROW, DORINO_REVIVE_ROW = 1, 3      # of its list: Tonic, Mid Tonic, Heal, Revive, Shelter
CURSED_WOODS, WOODS, FROGS_BURROW = 0x08C, "8c00", "8d00"
FROG = 4                    # in his burrow
FROG_ID = 4                 # in the party's record
WOODS_EXIT = (136, 760)     # tile 7-10, 47
STORY_FROG_JOINS = 0x84
MOUNTAINSIDE, MOUNTAINSIDE_NEAR = 0x0A3, (528, 690)
MAGIC_CAVE, MAGIC_CAVE_EXIT = "a400", (42 * 16 + 8, 7 * 16 + 8)
STORY_MAGIC_CAVE = 0x87
CASTLE_DOOR = (672, 472)
CASTLE_HALL, CASTLE_LEFT_WING = "a600", "d501"
FLEA_WAY_BACK = ("7d00", "ab00", "a600")
CASTLE_KNOWN_DOORS = {"a600", "ab00", "d501", "a500"}   # the hall's ways out the party has taken
OZZIES_CORRIDOR = "aa00"    # down through the hall's floor once both of Magus's generals are beaten
CORRIDOR_END_ROW = 130      # past the corridor's last statue pair
# The inner castle, room by room: each next map and its way in (the exits' tiles, from the rooms'
# exit records), the aim of the search toward it.
INNER_WAY = {
    "corridor_end": ("a700", (38 * 16 + 8, 4 * 16 + 8)),
    "tower": ("b000", (39 * 16 + 8, 4 * 16 + 8)),
    "tower_top": ("a800", (23 * 16 + 8, 3 * 16 + 8)),
    "bridge_room": ("be01", (58 * 16 + 8, 3 * 16 + 8)),
    "gallery": ("af00", (23 * 16 + 8, 3 * 16 + 8)),
    "antechamber": ("d601", (54 * 16 + 8, 4 * 16 + 8)),
    "magus_door": ("ae00", (38 * 16 + 8, 4 * 16 + 8)),
}    # beside the castle's door on the world map, where ctsave06 slot 2 stands

START = ["loaded", "settings", "forest_maze", "maze_out"]
NIZBEL = nizbel.PHASES[2:]
HOME = ["mystic_mountains", "end_of_time", "medina", "melchior", "masamune", "medina_gate", "truce_canyon_600",
        "zenan", "dorino", "cursed_woods", "frog", "woods_out", "mountainside"]
FLASHBACK = magic_cave.PHASES[2:]
CAVE = ["magic_cave", "cave_out"]
CASTLE = castle.APPROACH[1:]
SLASH = castle_slash.PHASES[len(castle.APPROACH):]
SLASH_BACK = ["slash_back", "tended_for_flea"]
FLEA = castle_flea.PHASES[len(castle.APPROACH):]
INNER = ["hall_again", "trapdoor", "ozzies_corridor", "corridor_end", "tower", "tower_top", "rested", "bridge_room",
         "gallery", "antechamber", "magus_door", "ozzie_falls", "rested_for_magus", "magus_stairs",
         "magus_chamber", "magus_fight", "magus"]
PHASES = START + NIZBEL + HOME + FLASHBACK + CAVE + CASTLE + ["tended_for_slash"] + SLASH + SLASH_BACK + FLEA + INNER
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"


def story(run):
    return run.machine.b(STORY)


def pillar(run, spot, want):
    """At the End of Time, take the pillar of light at `spot` to map `want`."""
    machine = run.machine
    main_leg.walking(run)
    for attempt in range(6):
        assert run.go(*spot, tolerance=1 if attempt else 4), "the pillar"
        machine.run(20)
        machine.run(3, "A")
        if run.dialogs_until(lambda: run.map_id() == want, 1500):
            assert main_leg.in_hand(run)
            return
    raise AssertionError(("the pillar to", want))


def world_toward(run, x, y, near=24):
    """On a world map, head for (x, y) until within `near` pixels of it."""
    run.wait_world_control()
    toward(run, x, y, lambda: abs(run.world_pos()[0] - x) + abs(run.world_pos()[1] - y) <= near,
           step=8, limit=2000, world=True)


def forest_maze(run):
    run.wait_world_control()
    world_enter(run, FOREST_MAZE)
    assert main_leg.in_hand(run, 20000)


def maze_out(run):
    main_leg.through(run, *MAZE_FOOT, lambda: run.map_id() == WORLD_65M and run.world_pos()[1] > BELOW_MAZE,
                     rounds=30, limit=MAZE_SEARCH)


def mystic_mountains(run):
    """Out of Ioka and up the Mystic Mountains to the Gate."""
    assert main_leg.leave_fighting(run, WORLD_65M), "out of Ioka"
    run.wait_world_control()
    world_enter(run, MYSTIC_MOUNTAINS)
    for want in (MOUNTAIN_PATH, MOUNTAIN_TOP):
        assert main_leg.leave_fighting(run, want), want


def end_of_time(run):
    """Onto the Gate: Ayla sees the party off, and the Gate takes it to the End of Time."""
    main_leg.walking(run)
    gx, gy = MYSTIC_GATE
    assert run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL)
                         or (abs(x - gx) <= 6 and abs(y - gy) <= 6)) is not None, "onto the Gate"
    for _ in range(4):
        if run.map_id() == END_OF_TIME:
            break
        assert main_leg.in_hand(run, 20000)
    assert main_leg.in_hand(run)
    assert run.map_id() == END_OF_TIME and story(run) == STORY_GATE_KEY_HOME, "the End of Time"


def medina(run):
    pillar(run, PILLAR_MEDINA, MEDINA_HOUSE)
    assert main_leg.leave_fighting(run, WORLD_1000), "out of the house"


def melchior(run):
    """Melchior takes the Masamune's pieces and the Dreamstone down to his forge, and Lucca with him."""
    run.wait_world_control()
    world_enter(run, MELCHIORS_HUT)
    main_leg.walking(run)
    assert run.talk(MELCHIOR), "Melchior"
    assert main_leg.in_hand(run)


def masamune(run):
    """Down after them to watch the sword forged."""
    main_leg.walking(run)
    assert run.walk(lambda m, x, y: m.peek(A.MAP_ID, 2).hex() == HUT_BELOW) is not None, "downstairs"
    assert run.dialogs_until(lambda: story(run) >= STORY_MASAMUNE, 20000), "the Masamune forged"
    assert main_leg.in_hand(run)


def medina_gate(run):
    """Back to the Medina house and through the Gate in its cupboard, confirmed facing it."""
    machine = run.machine
    assert main_leg.leave_fighting(run, WORLD_1000), "out of the hut"
    run.wait_world_control()
    world_enter(run, MEDINA_HOUSE_LOCATION)
    main_leg.walking(run)
    assert run.go(*MEDINA_CUPBOARD, tolerance=2), "below the cupboard"
    machine.run(1, "UP")
    machine.run(3, "A")
    assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME, 3000), "the End of Time"
    assert main_leg.in_hand(run)


def truce_canyon_600(run):
    pillar(run, PILLAR_600, TRUCE_CANYON_600)
    assert main_leg.leave_fighting(run, TRUCE_CANYON_600_FOOT), "the canyon's lower part"
    assert main_leg.leave_fighting(run, WORLD_600), "the 600 A.D. world map"


def zenan(run):
    """Over Zenan Bridge, north to south."""
    run.wait_world_control()
    world_enter(run, ZENAN_BRIDGE, row=ZENAN_NORTH_ROW)
    main_leg.walking(run)
    main_leg.through(run, *ZENAN_WEST, lambda: run.map_id() == WORLD_600, rounds=20)


def dorino(run):
    """Supplies from Dorino's shop, by the bridge's south end: the save has no Mid Tonics, and
    Magus's castle is long, with no save point to rest at before its last rooms."""
    machine = run.machine
    run.wait_world_control()
    world_enter(run, DORINO_SHOP)
    main_leg.buy(run, DORINO_COUNTER, [(DORINO_MID_TONIC_ROW, 50, 100), (DORINO_REVIVE_ROW, 2, 200)])
    run.say("supplies", "mid tonics", menus.count_of(machine, policies.MID_TONIC),
            "revives", menus.count_of(machine, policies.REVIVE))
    main_leg.walking(run)
    assert main_leg.leave_fighting(run, WORLD_600), "out of the shop"


def cursed_woods(run):
    run.wait_world_control()
    world_enter(run, CURSED_WOODS)
    assert main_leg.leave_fighting(run, FROGS_BURROW), "Frog's burrow"


def frog(run):
    """Frog, shown the Masamune, remembers, takes it and joins in Lucca's place."""
    main_leg.walking(run)
    assert run.talk(FROG), "Frog"
    for _ in range(4):
        if main_leg.in_hand(run, 30000) and run.map_id() == FROGS_BURROW:
            break
    assert story(run) == STORY_FROG_JOINS and FROG_ID in run.machine.peek(A.PARTY_MEMBERS, 3), "Frog joins"


def woods_out(run):
    """Out of the burrow and the woods. The burrow's door is beside where the party comes out:
    away from it first, so the walk out doesn't take it back in."""
    assert main_leg.leave_fighting(run, WOODS), "out of the burrow"
    main_leg.walking(run)
    assert run.walk_here(lambda m, x, y: y > 160) is not None, "away from the burrow's door"
    main_leg.through(run, *WOODS_EXIT, lambda: run.map_id() == WORLD_600, rounds=20)


def mountainside(run):
    world_toward(run, *MOUNTAINSIDE_NEAR)
    world_enter(run, MOUNTAINSIDE)
    assert run.map_id() == magic_cave.MOUNTAIN, "the mountainside"


def magic_cave_scene(run):
    """Frog's memories end; he cleaves the rock with the Masamune and the party is in the cave."""
    for _ in range(6):
        if main_leg.in_hand(run, 30000) and run.map_id() == MAGIC_CAVE:
            break
    assert run.map_id() == MAGIC_CAVE and story(run) == STORY_MAGIC_CAVE, "the Magic Cave"


def cave_out(run):
    search_walk(run, WORLD_600, MAGIC_CAVE_EXIT)
    world_toward(run, *CASTLE_DOOR, near=8)


HP, MAX_HP, MP, MAX_MP = 3, 5, 7, 9          # words of a character's record (playthrough.CHARACTER_RECORDS)
ITEM_TARGET_ROW = 12                          # the items' member window: its cursor 12-14 on the active rows
TONIC_HP, ETHER_MP, MID_ETHER_MP = 50, 10, 30


def record_word(run, character, offset):
    return run.machine.w(main_leg.CHARACTER_RECORDS + character * main_leg.CHARACTER_RECORD_BYTES + offset)


def tend(run):
    """Before a boss, the party tended from the bag on the menus' items screen: Tonics for the
    hurt and Ethers for spent magic, as a player would, there being no save point to rest at
    on the way (a Shelter only rests the party at one)."""
    machine = run.machine
    party = [who for who in machine.peek(A.PARTY_MEMBERS, 3) if who < 0x80]
    menus.open_menu(machine)
    menus.open_option(machine, menus.ITEMS)
    machine.run(20)

    def use(item, row, wanted):
        if not menus.count_of(machine, item) or not wanted():
            return
        menus.to_item(run, item)
        menus.press(machine, "A", 30)
        menus.press(machine, "A", 50)
        for _ in range(4):
            if machine.b(menus.CURSOR) == ITEM_TARGET_ROW + row:
                break
            menus.press(machine, "DOWN", 30)
        for _ in range(12):
            if not menus.count_of(machine, item) or not wanted():
                break
            menus.press(machine, "A", 60)
        menus.press(machine, "B", 40)

    for row, who in enumerate(party):
        use(policies.TONIC, row, lambda: record_word(run, who, HP) + TONIC_HP <= record_word(run, who, MAX_HP))
        use(policies.MID_ETHER, row, lambda: record_word(run, who, MP) + MID_ETHER_MP <= record_word(run, who, MAX_MP))
        use(policies.ETHER, row, lambda: record_word(run, who, MP) + ETHER_MP <= record_word(run, who, MAX_MP))
    menus.back_to_main_menu(machine)
    menus.press(machine, "B", 60)
    assert run.wait_control(), "the field back after the menus"
    run.say("tended", [(record_word(run, who, HP), record_word(run, who, MP)) for who in party])


MARLE, FROG_CHARACTER = 1, 4
AURA, CURE, SLURP = 0, 3, 0     # their places in Marle's and Frog's battle tech lists
CURE_MP, AURA_MP, SLURP_MP = 2, 1, 1
CRITICAL = 0.25                 # of a member's max HP: a Mid Tonic rather than a Tonic


def castle_fights(bot, state, rng, inventory):
    """The castle routes' fights (castle.castle_policy), with this party's own healing: Marle's
    Cure and Aura and Frog's Slurp heal whoever is lowest, and then a Tonic, the Mid Tonics kept
    for Magus where Tonics are left unless a member is near to falling."""
    member = state.member
    character = policies.party_ids(bot)[member]
    alive = [i for i in range(3) if state.hp(i)]
    if alive and len(alive) == 3:
        low = min(alive, key=lambda i: policies.fraction(state, i))
        if policies.fraction(state, low) < 0.5:
            mp = state.mp(member)
            if character == MARLE and mp >= CURE_MP and policies.tech_known(bot, member, CURE):
                return "tech", CURE, low
            if character == MARLE and mp >= AURA_MP and policies.tech_known(bot, member, AURA):
                return "tech", AURA, low
            if character == FROG_CHARACTER and mp >= SLURP_MP and policies.tech_known(bot, member, SLURP):
                return "tech", SLURP, low
            first, then = ((policies.MID_TONIC, policies.TONIC) if policies.fraction(state, low) < CRITICAL
                           else (policies.TONIC, policies.MID_TONIC))
            action = policies.use(inventory, first, low) or policies.use(inventory, then, low)
            if action:
                return action
    return castle.castle_policy(bot, state, rng, inventory)


SEARCH_ROOT = 400               # in-memory slots: the search's root, then its places from here up
STEP_FRAMES, SETTLE_FRAMES, SETTLE_LIMIT, PUSH_FRAMES = 8, 6, 40, 150
WAYS = ("UP", "DOWN", "LEFT", "RIGHT")
PUSHES = tuple("PUSH " + way for way in WAYS)   # a way held a while, against whatever stops the party


def hold(way):
    """A move's frames and buttons."""
    return (PUSH_FRAMES, way[len("PUSH "):]) if way in PUSHES else (STEP_FRAMES, way)
FLEE_SLOT, FLEE_FRAMES = 3980, 6000
LONG_FIGHT = 250000     # frames: the four statues before Magus's hall take the party's blows a few points at a time


def search_through(run, want, aim, max_places=3000, wait=False):
    """Walk to map `want`, searching copies nearest `aim` first, where the field takes the party
    out of the pad's hands on the way: a ladder climbed, a step on a moving stair. Each move
    plays on until the party is in hand again (or a fight or window stops it), and where that
    leaves the party is the place searched from. A fight or a window that stops a move ends the
    search there, to be settled; the caller searches again. With `wait`, pushing a while against
    whatever stops the party is a move too: an enemy that guards the way (on the castle's stairs)
    fights a party that keeps pressing on it, but not one that walks on by."""
    import heapq
    machine = run.machine
    here = run.map_id()
    machine.mark(SEARCH_ROOT)
    queue, seen, used, found = [(0, SEARCH_ROOT, ())], set(), [], None
    try:
        while queue and len(used) < max_places and found is None:
            _, slot, path = heapq.heappop(queue)
            for way in WAYS + (PUSHES if wait else ()):
                machine.restore(slot)
                machine.probe(*hold(way))
                waited = 0
                while not machine.b(A.FIELD_PLAYER_CONTROL) and waited < SETTLE_LIMIT and run.map_id() == here:
                    machine.probe(SETTLE_FRAMES)
                    waited += 1
                move = path + ((way, waited),)
                if run.map_id() == want or run.dialog() or A.battle_menu_open(machine) or (
                        waited >= SETTLE_LIMIT and run.map_id() == here):
                    found = move
                    break
                if run.map_id() != here or way in PUSHES:
                    continue
                x, y = run.me()
                key = (int(x) // 4, int(y) // 4)
                if key in seen:
                    continue
                seen.add(key)
                child = SEARCH_ROOT + 1 + len(used)
                machine.mark(child)
                used.append(child)
                heapq.heappush(queue, (len(move) * STEP_FRAMES + 4 * (abs(x - aim[0]) + abs(y - aim[1])), child, move))
    finally:
        machine.restore(SEARCH_ROOT)
        machine.free(SEARCH_ROOT)
        for slot in used:
            machine.free(slot)
    assert found is not None, ("no way to", want, "from", here, run.me(), len(seen))
    for way, waited in found:
        machine.run(*hold(way))
        machine.run(SETTLE_FRAMES * waited)
    run.say("search_through", want, "moves", len(found), "places", len(seen), "now", run.map_id(), run.me())


def settle_fight(run):
    """A fight on the way: run from it, the party holding L and R, and only where that fails
    (a fight the game won't let it leave) fight it with castle_fights. The Magic Cave's and the castle's inner
    rooms are full of foes the party's blows hardly touch, and nothing on the way needs them
    beaten."""
    if not early.battle_ready(run):
        return
    machine = run.machine
    machine.mark(FLEE_SLOT)
    try:
        main_leg.flee(run, FLEE_FRAMES)
    except AssertionError:
        machine.restore(FLEE_SLOT)
        main_leg.win(run, policy=castle_fights, max_frames=LONG_FIGHT)
    finally:
        machine.free(FLEE_SLOT)


def search_walk(run, want, aim, rounds=20):
    """search_through to map `want`, the fights on the way run from (settle_fight)."""
    for _ in range(rounds):
        if run.map_id() == want:
            return
        run.dialogs_until(lambda: early.battle_ready(run) or run.machine.b(A.FIELD_PLAYER_CONTROL), 3000)
        settle_fight(run)
        assert main_leg.in_hand(run, 30000, policy=castle_fights), "the field in hand"
        if run.map_id() == want:
            return
        main_leg.walking(run)
        try:
            search_through(run, want, aim)
        except AssertionError:
            search_through(run, want, aim, wait=True)
    assert run.map_id() == want, ("too many stops on the way to", want)


def slash_back(run):
    """From Slash's room back through the left wing to the hall."""
    for want in (CASTLE_LEFT_WING, CASTLE_HALL):
        assert run.exit_to(want), want
        assert run.wait_control()


def hall_again(run):
    """From Flea's room back to the hall."""
    for want in FLEA_WAY_BACK:
        assert run.exit_to(want), want
        assert run.wait_control()


def trapdoor(run):
    """Both generals beaten, the hall's floor gives way under the party: down to Ozzie's
    corridor."""
    main_leg.walking(run)
    assert run.walk(lambda m, x, y: m.peek(A.MAP_ID, 2).hex() not in CASTLE_KNOWN_DOORS
                    or not m.b(A.FIELD_PLAYER_CONTROL)) is not None, "the hall's floor"
    for _ in range(4):
        if run.map_id() == OZZIES_CORRIDOR:
            break
        assert main_leg.in_hand(run, 6000, policy=castle_fights)
    assert run.map_id() == OZZIES_CORRIDOR, "Ozzie's corridor"
    assert main_leg.in_hand(run, policy=castle_fights)


def ozzies_corridor(run):
    """Up the corridor: its statues come alive as the party passes each pair."""
    goal = lambda: run.map_id() != OZZIES_CORRIDOR or run.me()[1] < CORRIDOR_END_ROW
    for _ in range(12):
        assert early.advance(run, "UP", goal), ("blocked in the corridor", run.me())
        if goal():
            return
        run.dialogs_until(lambda: early.battle_ready(run) or run.machine.b(A.FIELD_PLAYER_CONTROL), 3000)
        settle_fight(run)
        assert main_leg.in_hand(run, policy=castle_fights)
    raise AssertionError("too many fights in the corridor")


ON_A_SAVE_POINT = (0x7F01CF, 0x80)   # story_game_notes: the leader stands on a save point
CASTLE_SAVE_ROOM, CASTLE_SAVE_DOOR = "d701", (808, 60)   # off the tower's top, through its north door
CASTLE_SAVE_POINT = (24, 272)
CASTLE_SAVE_ROOM_DOOR = (248, 80)   # the dark room's way back out, where the party comes in


def rest_at_save_point(run, spot):
    """On the save point at `spot`, a Shelter from the bag: every member's HP and MP restored."""
    machine = run.machine
    main_leg.walking(run)
    assert run.go(*spot, tolerance=2), "onto the save point"
    machine.run(30)
    address, bit = ON_A_SAVE_POINT
    assert machine.b(address) & bit, "standing on the save point"
    shelters = menus.count_of(machine, menus.SHELTER)
    assert shelters, "a Shelter"
    menus.open_menu(machine)
    menus.open_option(machine, menus.ITEMS)
    menus.use_item(run, menus.SHELTER, choose_member=True)
    machine.run(180)
    menus.back_to_main_menu(machine)
    menus.press(machine, "B", 60)
    assert run.wait_control(), "the field after resting"
    party = [who for who in machine.peek(A.PARTY_MEMBERS, 3) if who < 0x80]
    run.say("rested", [(record_word(run, who, HP), record_word(run, who, MP)) for who in party])
    assert all(record_word(run, who, HP) == record_word(run, who, MAX_HP) for who in party), "HP restored"


def rested(run):
    """Into the dark room off the tower's top, where a save point is, to rest with a Shelter
    before the castle's last rooms (the bag holds no Ethers for Ozzie's barrier), and back."""
    search_walk(run, CASTLE_SAVE_ROOM, CASTLE_SAVE_DOOR)
    rest_at_save_point(run, CASTLE_SAVE_POINT)
    search_walk(run, INNER_WAY["tower_top"][0], CASTLE_SAVE_ROOM_DOOR)


OZZIE, OZZIE_SPOT = 10, (856, 838)     # in his hall before Magus's: spoken to from below him
STORY_OZZIE_FALLS = 0x89
MAGUS_HALL_SAVE_POINT = (904, 840)     # the right of the two sparkles beside where Ozzie stood
MAGUS_STAIRS, MAGUS_STAIRS_WAY = "bf01", (808, 840)   # the left sparkle takes the party down
MAGUS_CHAMBER, MAGUS_CHAMBER_DOOR = "ac00", (15 * 16 + 8, 29 * 16 + 8)


def levers(bot, state, rng, inventory):
    """Ozzie's last stand: his barrier answers every blow, so the party strikes the levers
    beside him instead; each opens the floor under him, until he falls for good."""
    others = [slot for slot in policies.enemies(state) if slot != OZZIE_SLOT]
    if others:
        return "attack", others[0]
    return castle_fights(bot, state, rng, inventory)


OZZIE_SLOT = 3                          # the battle's first enemy slot; the levers come after


def ozzie_falls(run):
    """Ozzie, spoken to, fights from behind his barrier; the levers drop him."""
    machine = run.machine
    main_leg.walking(run)
    assert run.go(*OZZIE_SPOT, tolerance=3), "below Ozzie"
    machine.run(4, "UP")
    machine.run(10)
    machine.run(3, "A")
    assert run.dialogs_until(lambda: early.battle_ready(run), 12000), "Ozzie's fight"
    for _ in range(8):
        in_hand = main_leg.in_hand(run, 30000, policy=levers)
        if in_hand and story(run) >= STORY_OZZIE_FALLS:
            break
    assert story(run) == STORY_OZZIE_FALLS, "Ozzie gone"


def rested_for_magus(run):
    rest_at_save_point(run, MAGUS_HALL_SAVE_POINT)


def magus_stairs(run):
    """The left sparkle takes the party down onto the dark stair to Magus."""
    main_leg.walking(run)
    gx, gy = MAGUS_STAIRS_WAY
    assert run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL)
                         or (abs(x - gx) <= 3 and abs(y - gy) <= 3)) is not None, "the left sparkle"
    for _ in range(4):
        if run.map_id() == MAGUS_STAIRS:
            break
        main_leg.in_hand(run, 6000, policy=castle_fights)
    assert run.map_id() == MAGUS_STAIRS, "the stair down"
    assert main_leg.in_hand(run, policy=castle_fights)


def magus_chamber(run):
    search_walk(run, MAGUS_CHAMBER, MAGUS_CHAMBER_DOOR)


# Magus: his barrier leaves him open to one element at a time, which his record's four
# elemental affinities say (bit 7 absorbs; the low bits the scale's divisor, the least the
# weakest), and a blow moves it on. Lightning (Crono's) and water (Marle's Ice) are this party's.
COMBATANT_RECORDS = 0x7E5E2D          # battle/combatant.h's records: A.BATTLE_STATS less STAT_HP
STAT_ELEMENT_AFFINITY, ABSORBS, DIVISOR = 0x3F, 0x80, 0x3F
LIGHTNING_ELEMENT, WATER_ELEMENT = 0, 2   # their places among the four
BATTLE_MENU_QUEUE = 0x7E95D6          # the members whose menus wait to open: ready for a double tech
CRONO_CHARACTER = 0
LIGHTNING_TECH, ICE_TECH, AURA_WHIRL_TECH, X_STRIKE_TECH = 3, 11, 57, 66   # tech ids
MAGUS_SPOT = (120, 230)               # below Magus in his chamber
MAGUS_FIGHT, MAGUS_TRIES = 400000, 5000   # frames, and tries back over wipes


def affinities(machine, slot):
    at = COMBATANT_RECORDS + slot * A.BATTLE_STAT_STRIDE + STAT_ELEMENT_AFFINITY
    return list(machine.peek(at, 4))


def magus_policy(bot, state, rng, inventory):
    """Magus's fight: the spell his barrier is open to when it is this party's, Crono and Frog's
    X-Strike (or Crono and Marle's Aura Whirl, to heal all) when the partner is ready too, Marle's
    Cure and Haste, Mid Tonics and Revives; otherwise blows, which turn his barrier on."""
    machine = bot.machine
    member = state.member
    ids = list(policies.party_ids(bot))
    character = ids[member]
    living = policies.enemies(state)
    target = living[0] if living else None
    alive = [i for i in range(3) if state.hp(i)]
    entries = dict((tech, entry) for entry, tech in policies.tech_list_entries(bot, member))
    waiting = machine.peek(BATTLE_MENU_QUEUE, 4)
    ready = lambda who: who in ids and ids.index(who) in waiting
    mp = state.mp(member)
    for slot in range(3):
        if not state.hp(slot):
            action = policies.use(inventory, policies.REVIVE, slot)
            if action:
                return action
    low = min(alive, key=lambda i: policies.fraction(state, i))
    hurt = sum(1 for i in alive if policies.fraction(state, i) < 0.6)
    if (character == CRONO_CHARACTER and hurt >= 2 and len(alive) == 3 and ready(MARLE)
            and AURA_WHIRL_TECH in entries and mp >= 2):
        return "tech", entries[AURA_WHIRL_TECH], None
    if policies.fraction(state, low) < 0.5:
        if character == MARLE and mp >= CURE_MP:
            return "tech", CURE, low
        action = policies.use(inventory, policies.MID_TONIC, low) or policies.use(inventory, policies.TONIC, low)
        if action:
            return action
    if target is not None:
        open_to = min(range(4), key=lambda e: (affinities(machine, target)[e] & ABSORBS,
                                               affinities(machine, target)[e] & DIVISOR))
        if character == CRONO_CHARACTER and open_to == LIGHTNING_ELEMENT and LIGHTNING_TECH in entries and mp >= 2:
            return "tech", entries[LIGHTNING_TECH], target
        if character == MARLE and open_to == WATER_ELEMENT and ICE_TECH in entries and mp >= 2:
            return "tech", entries[ICE_TECH], target
    if character == CRONO_CHARACTER and ready(FROG_CHARACTER) and X_STRIKE_TECH in entries and mp >= 2:
        return "tech", entries[X_STRIKE_TECH], target
    if character == MARLE and mp >= 6 and rng.random() < 0.2:
        return "tech", 4, rng.choice([0, 1])
    return "attack", target


def magus_fight(run):
    """Up to Magus, and his words, to the fight's first command."""
    machine = run.machine
    main_leg.walking(run)
    stop = lambda: early.battle_ready(run) or run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL)
    for _ in range(6):
        early.advance(run, "UP", stop)
        machine.run(3, "A")
        if run.dialogs_until(lambda: early.battle_ready(run), 30000):
            break
        main_leg.walking(run)
    assert early.battle_ready(run), "Magus's fight"


def magus(run):
    main_leg.win(run, policy=magus_policy, max_frames=MAGUS_FIGHT, max_tries=MAGUS_TRIES)


def inner_room(name):
    want, aim = INNER_WAY[name]
    return lambda run: search_walk(run, want, aim)


STEPS = {
    "forest_maze": forest_maze, "maze_out": maze_out,
    "mystic_mountains": mystic_mountains, "end_of_time": end_of_time, "medina": medina,
    "melchior": melchior, "masamune": masamune, "medina_gate": medina_gate,
    "truce_canyon_600": truce_canyon_600, "zenan": zenan, "dorino": dorino, "cursed_woods": cursed_woods,
    "frog": frog, "woods_out": woods_out, "mountainside": mountainside,
    "magic_cave": magic_cave_scene, "cave_out": cave_out, "slash_back": slash_back,
    "tended_for_slash": tend, "tended_for_flea": tend,
    "hall_again": hall_again, "trapdoor": trapdoor, "ozzies_corridor": ozzies_corridor, "rested": rested,
    "ozzie_falls": ozzie_falls, "rested_for_magus": rested_for_magus, "magus_stairs": magus_stairs,
    "magus_chamber": magus_chamber, "magus_fight": magus_fight, "magus": magus,
}
STEPS.update({name: inner_room(name) for name in INNER_WAY})


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)

    def own(run, start, offset, phases):
        """This route's own `phases`, PHASES' indices `offset` on, from `start`."""
        for index, name in enumerate(phases, offset):
            if start <= index:
                STEPS[name](run)
                run.say(name, run.map_id(), run.me(), "world", run.world_pos(), "story", hex(story(run)))
                run.checkpoint(name)

    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, WORLD_65M, STORY_BEFORE_MAZE)
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        own(run, start, 2, START[2:])
        at = len(START)
        if start < at + len(NIZBEL):
            nizbel.play(run, max(2, start - at + 2), args.seed)
        at += len(NIZBEL)
        own(run, start, at, HOME)
        at += len(HOME)
        if start < at + len(FLASHBACK):
            magic_cave.play(run, max(2, start - at + 2))
        at += len(FLASHBACK)
        own(run, start, at, CAVE)
        at += len(CAVE)
        castle_run = castle.CastleRun.adopt(run)
        if start < at + len(CASTLE):
            castle.approach(castle_run, max(1, start - at + 1), args.seed)
        at += len(CASTLE)
        own(castle_run, start, at, ["tended_for_slash"])
        at += 1
        if start < at + len(SLASH):
            castle_slash.play(castle_run, start - at + len(castle.APPROACH), args.seed, castle_fights)
        at += len(SLASH)
        own(castle_run, start, at, SLASH_BACK)
        at += len(SLASH_BACK)
        if start < at + len(FLEA):
            castle_flea.play(castle_run, start - at + len(castle.APPROACH), args.seed, castle_fights)
        at += len(FLEA)
        own(castle_run, start, at, INNER)
        run.finish("the leg's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
