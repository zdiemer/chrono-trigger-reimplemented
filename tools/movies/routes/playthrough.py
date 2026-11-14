"""A playthrough from a new game, on a blank cartridge, toward an ending.

The story's opening is played here: the battle mode and names, Crono's
house, the Millennial Fair, Marle and Lucca's telepod. From the 600 A.D.
world map on, the route goes on with the pieces other routes play from
cartridge saves (the Cathedral, the trial, ...), from the state this route
reached instead of a save. Each phase ends at a checkpoint and the route goes
on only past checkpoints that succeeded. It ends with Masa and Mune beaten
in the Denadoro Mountains, 600 A.D., and the Masamune's blade found.
"""
import importlib
import os

from .. import addresses as A
from .. import policies
from .menu_sweep import SHELTER, count_of
from ..campaign import REPO, Run, route_arguments, run_route
from ._trial_walk import field_exit, toward, world_enter
from . import _trial_party as early
from . import _trial_cathedral as cathedral
from . import cronos_trial
from . import dragon_tank
from . import guardian
from . import proto_dome
from . import r_series
from . import heckran
from . import heckran_prepare
from . import zombor

OPENING = ["named", "home", "fair", "collided", "joined", "exhibit", "gate", "truce_canyon", "guardia",
           "upstairs", "chamber", "kidnapped"]
# Then the pieces other routes play from saves: the Cathedral (after its
# save's loading) and Crono's trial.
CATHEDRAL = cathedral.PHASES[1:]
ESCAPE = ["rescued", "escaped"]
TANK = dragon_tank.PHASES[2:]
# Out of the castle and through the forest's Gate to 2300 A.D., across Lab 16
# to Arris Dome, and the Guardian there (its piece, after its save's loading).
FUTURE = ["castle_gate", "forest_chase", "forest_back", "future", "lab16", "lab16_east", "arris"]
GUARDIAN = guardian.PHASES[1:]
# Past the Guardian, through its room's door to the information center.
INFO = ["info_center", "lavos_day"]
# The rat's password, the sealed doors, the storeroom and Doan's Bike Key.
KEY = ["rat", "sealed_door", "storeroom", "bike_key"]
# Lab 32's race to Proto Dome, Robo repaired, and the factory (their pieces
# after their saves' loading; the factory's hall is entered here).
LAB32 = ["lab32", "johnny", "raced"]
PROTO = proto_dome.PHASES[1:]
FACTORY = ["factory_hall"] + r_series.PHASES[2:]
# The End of Time: Gaspar, Spekkio's laps and magic; then Heckran's Cave by
# the Medina gate (its piece, without its save) and Heckran.
END_OF_TIME = ["gaspar", "spekkio_door", "spekkio", "laps", "magic"]
CAVE = heckran_prepare.PHASES[1:-1] + ["cave_rest"]
HECKRAN = heckran.PHASES[1:]
# Out by the whirlpool, Leene Square's Gate to the End of Time and the pillar
# to 600 A.D.; Zenan Bridge's supplies from Guardia's kitchen, and Zombor
# (its piece, from the bridge).
ZENAN = (["whirlpool", "leene_gate", "truce_600", "zenan_captain", "chef", "jerky", "zenan_bridge"]
         + zombor.PHASES[3:])
# South over the bridge: Dorino's supplies, the Denadoro Mountains' foot (where
# ctsave04 slot 1 stands), up the mountains to the save point below their top,
# and Masa and Mune, who keep the Masamune's blade.
DENADORO = ["dorino_supplies", "denadoro_foot", "denadoro", "denadoro_falls", "denadoro_trained", "masa_mune"]
# The Masamune's hilt: Tata's Hero Medal in Porre, shown to Frog in his burrow;
# Melchior in 1000 A.D. (through Truce Canyon's Gate, the End of Time and the
# Medina pillar), who needs a Dreamstone; 65,000,000 B.C. by the End of Time's
# south-west pillar, Ioka's feast and Ayla's soup race for the Dreamstone, and
# Ayla joining when the Gate Key is found gone (story $78, where leg B's save,
# ctsave05 slot 1, stands).
DREAMSTONE = ["hero_medal", "bent_hilt", "melchior_told", "prehistory", "ioka_feast", "soup_race", "ayla_joins"]
# From the Forest Maze on, leg B's own play (route playthrough_leg-b, from its save's
# place), its checkpoints named apart from this route's own (Run.chapter).
LEG_B = importlib.import_module(".playthrough_leg-b", __package__)
LEG_B_CHAPTER = "b_"
LEG_B_FROM = LEG_B.PHASES.index("forest_maze")
FROM_LEG_B = [LEG_B_CHAPTER + name for name in LEG_B.PHASES[LEG_B_FROM:]]
PHASES = (OPENING + CATHEDRAL + cronos_trial.PHASES + ESCAPE + TANK + FUTURE + GUARDIAN + INFO + KEY
          + LAB32 + PROTO + FACTORY + END_OF_TIME + CAVE + HECKRAN + ZENAN + DENADORO + DREAMSTONE + FROM_LEG_B)
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# A playthrough from a new game (route playthrough), on a blank cartridge.
# frames {FRAMES}
# <start_frame> <frame_count> <buttons>"""

MENU_SCREEN = 0x7E0067      # which of the menus' screens is open
NAMING_SCREEN = 0x0D        # name entry (and, on a new game, the battle mode first)
EXCHANGE_SCREEN = 7         # the party exchange, which a member joining a full party opens
CURSOR = 0x7E0054
GOLD = 0x7E2C53             # three bytes
LEENE_SQUARE = 0x005        # the Millennial Fair's lower square
MARLE_AT_THE_BELL = 3       # an object of the bell's plaza
FAIR_FLAGS, PENDANT_FOUND = 0x7F0054, 0x20
TRUCE_CANYON_600 = "7100"
MARLE_AT_THE_EXHIBIT = 4
GUARDIA_FOREST_600, GUARDIA_CASTLE_600 = 0x077, 0x078
CATHEDRAL_SAVE_SPOT = (200, 272)
TRUCE_600, TRUCE_600_SHOP = (384, 288), 0x076
TRUCE_MARKET, TRUCE_MARKET_COUNTER = 0x011, (360, 680)   # 1000 A.D.
TRUCE_TONIC_ROW, TRUCE_REVIVE_ROW = 5, 7
SHOP_COUNTER = (896, 150)      # below the merchant, in the village shop
TONIC_ROW, REVIVE_ROW, SHELTER_ROW = 4, 6, 7   # of its list: Iron Blade, Dart Gun, Karate Gi, Bronze Helm, Tonic, Heal, Revive, Shelter
CHANCELLOR = 9                 # in the queen's tower
FAIR_NEWS, SHOW_STARTED = 0x7F0055, 0x80
LOWER_SQUARE_FAIRGOERS = (17, 16, 22, 24)   # who tell of Lucca's show
SHOP_SCREEN, SHOP_BUY, SHOP_COUNT = 12, 8, 10
OPENING_FLYOVER = "f001"    # the 1000 A.D. world map, which the opening flies over
CRONOS_ROOM = "0200"
GUARDIA_CASTLE_1000, GUARDIA_FOREST_1000, FOREST_GATE = "1500", "1300", "1400"
GATE_IN_THE_FOREST = 9      # an object of the forest's back, where the Gate opens
BANGOR_DOME, WORLD_2300 = "d000", "f201"
WORLD_1000, WORLD_600 = "f001", "f101"
LAB16_WEST, LAB16_EAST = "d400", "d500"
LAB16_DOOR = (264, 368)     # on the 2300 A.D. map, north of Bangor Dome
LAB16_WEST_EXIT = (504, 120)   # tile column 31, rows 5-9, to the east part
LAB16_EAST_EXIT = (952, 536)   # tile columns 57-61, row 33, out by Arris Dome
ARRIS_DOME = 0x0D6
INFO_CENTER = "5801"           # location $158, through the Guardian room's north door
INFO_CENTER_DOOR = (128, 340)  # below it, in the Guardian's room
COMPUTER = 8                   # an object of the information center
ARRIS_FLAGS, COMPUTER_SEEN = 0x7F00EC, 0x10
SEED = 0xD4
BIKE_KEY = 0xD5
DOME_HALL, DOME_INNER, DOME_LOWER, GUARDIAN_ROOM = "d600", "d800", "dd00", "db00"
PASSAGE, STORE_HALL, STOREROOM = "d700", "d900", "da00"
RAT = 13                       # the room below's rat at its foot; another waits at its head
RAT_TOLD = 0x40                # ARRIS_FLAGS: the password heard
RAT_TRIGGER_STEPS = 6          # up from the room below's foot onto the tile that sets the rat running
RAT_INTERCEPT = (96, 330)      # beside its run, west along the pipe, before it turns north
TALK_REACH, RAT_PIXELS_A_FRAME, RAT_MARGIN_FRAMES = 28, 2, 1   # the party arrives with a frame to spare
INNER_DOOR = 9                 # of the inner room's two sealed doors, the one to the storeroom's way
STORE_DOOR = 8                 # the sealed door on the way, in the store hall
STORY_STORE_SEEN = 0x36        # the storeroom's scene: the party sees the food is gone
STORY_BIKE_KEY = 0x37
LAB32_WEST, LAB32_EAST, RACE = "df00", "e100", "0f01"
LAB32_ENTRANCE, FACTORY_ENTRANCE = 0x0DF, 0x0E4
JOHNNY, JET_BIKE_SPOT = 13, (693, 118)   # Johnny, and below the jet bike, in Lab 32's west end
BIKE_APPROACH = (683, 112)
RACE_MOVIE, RACE_STARTS, RACE_ENDS = "movies/saves/events-jetbike-race-won.ctm", 3971, 8083
DOORS_OPEN = 0x7F00A4
ROBO = 3
END_OF_TIME_HALL, SPEKKIO_ROOM = "d001", "d101"
GASPAR, GATE_KEEPER = 28, 9       # the End of Time's: Gaspar by the lamp, and the one by the door
SPEKKIO = 8                       # in his room
EXCHANGE_SCREEN = 7               # the menus' screen that asks who stays at the End of Time
LAP_POSTS = (16, 17, 18, 19, 20)  # Spekkio's room: the posts along the wall, clockwise from the door
DOOR_POST = 16
LAP_COUNTS = (9, 10, 11, 12, 13)  # event variables: laps past each post
LAPS_DONE, CHEATED = 14, 4        # event variable: laps finished, or 4 for a post missed or doubled
SPEKKIO_SPOT = (120, 178)         # below him, facing up
EVENT_VARIABLES = 0x7F0200        # a word each
TOUCHING = 2                      # an object's call level while its touch runs
SAVE_POINT_FLAGS, ON_A_SAVE_POINT = 0x7F01CF, 0x80
STORY_SPEKKIO_DOOR, STORY_SPEKKIO_MET, STORY_MAGIC = 0x4B, 0x4C, 0x4D
HECKRANS_CHAMBER, WHIRLPOOL = "2f00", 3      # the object Heckran's chamber leaves by, into the water
LEENE_EXHIBIT, LEENE_GATE = "0800", (140, 90)  # the Gate between the telepods, 1000 A.D.
GATE_PLATFORM_DOOR = 0x7F020E                 # the End of Time: the platform reached
LEADER_TILE_X, LEADER_TILE_Y = 0x7F0212, 0x7F0214
TRUCE_PILLAR = (12, 7)                        # the pillar of light to Truce Canyon, 600 A.D.
STORY_600_AGAIN = 0x54
ZENAN_BRIDGE_600, ZENAN_CAPTAIN = 0x087, 13
GUARDIA_KITCHEN, CASTLE_HALL, CHEF = "7b00", "7800", 10
KITCHEN_DOOR = (48 * 16 + 8, 34 * 16 + 8)     # the hall's right-hand exit
ZENAN_FLAGS = 0x7F00A9
SUPPLIES_ASKED, CHEF_TOLD, JERKY_GIVEN = 0x04, 0x08, 0x10
JERKY = 0xDB
BENT_SWORD = 0x50
CHARACTER_RECORDS, CHARACTER_RECORD_BYTES, RECORD_HP, RECORD_LEVEL = 0x7E2600, 0x50, 3, 0x12
ENERTRON = 14                   # Proto Dome's: a rest that restores HP and MP
ARRIS_MERCHANT = 11
ARRIS_TONIC_ROW, ARRIS_REVIVE_ROW = 3, 5     # of its list: Auto Gun, Iron Suit, Iron Helm, Tonic, Heal, Revive, Shelter
ARRIS_COUNTER_BELOW = 24           # Arris Dome's sealed doors, a bit each
SHADOW = 0x66                # an enemy number: Lab 16's, 1 HP, no harm from weapons
RUN_FROM = {SHADOW}
DORINO_SHOP, DORINO_COUNTER = 0x08B, (896, 424)   # below the merchant
DORINO_TONIC_ROW, DORINO_MID_TONIC_ROW = 0, 1      # of its list: Tonic, Mid Tonic, Heal, Revive, Shelter
DENADORO_FOOT = (424, 576)       # on the 600 A.D. map, before the mountains' entrance
DENADORO_MTS, DENADORO_ENTRANCE, DENADORO_WAY_UP = 0x091, "9100", "8e00"
STORY_ZOMBOR, STORY_DENADORO, STORY_BLADE = 0x5A, 0x5D, 0x60
# The entrance's three ways up lead to three parts of the next map ($08E); the
# west one (tile 11, row 1) goes on up its ladders.
DENADORO_WEST_WAY = (11 * 16 + 8, 64)
# Then map by map, each the next map and where its way in is (the exits' tiles,
# from the locations' exit records): the climbs and the falls to the save point
# below the top ($095), and from there the top ($08F's west part) and the cave
# where Masa and Mune wait ($097).
DENADORO_CLIMB = (("9200", (504, 24)), ("8f00", (8, 72)), ("9300", (504, 56)), ("9600", (568, 24)),
                  ("9000", (8, 168)), ("9400", (520, 264)), ("9500", (56, 504)))
DENADORO_TOP = (("8f00", (1016, 456)), ("9700", (264, 56)))
# Training below the save point: the monsters of the map before it ($094),
# which come back each time the party steps out and in again.
DENADORO_AWAY, DENADORO_BACK = ("9400", (968, 24)), ("9500", (56, 504))
# Masa and Mune at levels 13-14 took the party 18 tries (44 before Crono's
# Slash answered their Tornado energy); at 15 and up a handful.
MASA_AND_MUNE_LEVEL = 15
PORRE_NEAR, TATAS_HOUSE, TATA = (408, 870), 0x098, 9
STORY_HERO_MEDAL, HERO_MEDAL = 0x66, 0xB3
CURSED_WOODS, FROGS_BURROW = 0x08C, "8d00"
FROG_IN_THE_BURROW, FROGS_BASKET = 11, 10   # the basket he stands before, which he moves from for the medal
STORY_HILT, BENT_HILT = 0x69, 0x51
TRUCE_CANYON_NEAR, TRUCE_CANYON = (440, 248), 0x070
CANYON_GATE_SPOT = (114, 124)               # left of the Gate, facing it
STORY_MELCHIOR = 0x6C
PILLAR_65M, MYSTIC_MOUNTAIN_TOP, WORLD_65M = (200, 199), "1001", "f301"
MOUNTAIN_WAY_DOWN = ("1101", "1201", WORLD_65M)
STORY_PREHISTORY = 0x6F
CHIEFS_HUT, CHIEFS_HUT_MAP, FEAST, AYLA_IN_THE_HUT = 0x113, "1301", "1801", 6
FEAST_AYLA = 6
# Whom the party speaks to at the feast, a round at a time, before Ayla's
# challenge: found by playing it (the talks count in the objects' scripts).
FEAST_TALKS = ((3, 4, 5, FEAST_AYLA), (FEAST_AYLA,), (3, 5))
SOUP_RACE_WINDOWS = 9                       # the challenge's windows, to "Push A Button much times!"
STORY_SOUP, STORY_DREAMSTONE, STORY_AYLA = 0x72, 0x75, 0x78
DREAMSTONE_ITEM, GATE_KEY = 0xDC, 0xD7
AYLA_ASLEEP_REACH = (171, 191)              # up to her row, then right until she stops the party
AYLA = 5
CRONO, MARLE, LUCCA = 0, 1, 2
SLASH, ICE, FIRE, AURA = 1, 2, 2, 0   # their places in Crono's, Marle's and Lucca's tech lists
SLASH_MP = ICE_MP = FIRE_MP = 2
AURA_MP = 1
MASA_AND_MUNE_AS_ONE_HP = 3600
ENEMY_KIND_SLOTS = 4         # words at A.BATTLE_ENEMY_KINDS
WAIT_MODE = 1              # the battle mode question's second row: Wait


def naming(run):
    """A name entry screen is open: no field to walk and no window to close."""
    machine = run.machine
    return (machine.b(MENU_SCREEN) == NAMING_SCREEN and not run.dialog()
            and not machine.b(A.FIELD_PLAYER_CONTROL))


def enemy_kinds(machine):
    """The enemy numbers the open battle holds (battle_enemy_kinds). The
    battle counts only the enemies shown at its start, so a fight that opened
    with nothing but hidden Shadows would name none and fail the route's
    check below; none of the playthrough's fights does."""
    words = [machine.w(A.BATTLE_ENEMY_KINDS + 2 * i) for i in range(ENEMY_KIND_SLOTS)]
    return {word & 0xFF for word in words if word < 0x8000}


def foes_with_hit_points(machine):
    """Whether the open battle has an enemy the bot can wear down."""
    for slot in range(3, 11):
        base = A.BATTLE_STATS + slot * A.BATTLE_STAT_STRIDE
        if 1 < machine.w(base + 2) < 65000 and machine.w(base) > 0:
            return True
    return False


def flee(run, max_frames=3000):
    """Hold L and R until the party has run from the fight."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        machine.run(20, "L+R")
        if machine.b(A.FIELD_PLAYER_CONTROL) and not A.battle_menu_open(machine):
            run.say("fled", machine.frame - start, "frames")
            return
    raise AssertionError("the party could not run")


def win(run, seed=1, policy=early.early_party, max_frames=35000, max_tries=300):
    """Fight the battle that is open to its end: every enemy down, or the
    field given back (a fight a scene ends, as the prison's guards' does,
    leaves its enemies standing). Lab 16's Shadows, which have 1 HP and take
    no harm from a weapon, are run from; any other fight with no enemy the
    bot can wear down fails the route. `policy` chooses the party's actions; a fight
    still going after `max_frames`, or lost after `max_tries` tries back, fails the route."""
    machine = run.machine
    kinds = enemy_kinds(machine)
    run.say("enemies", sorted(kinds))
    if kinds and kinds <= RUN_FROM:
        flee(run)
        return
    assert foes_with_hit_points(machine), ("no enemy the bot can wear down", sorted(kinds))
    seen, field_frames = [], [0]

    def over(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        if seen and not living and any(state.hp(slot) for slot in range(party_size(r))):
            return True
        field_frames[0] = field_frames[0] + 1 if machine.b(A.FIELD_PLAYER_CONTROL) else 0
        return field_frames[0] >= 60

    run.fight(policy, over, party=party_size(run), seed=seed, max_frames=max_frames, max_tries=max_tries)


def catch_the_rat(run, max_nodes=2500):
    """Set the room below's rat running from its foot, dash to RAT_INTERCEPT by
    the shortest way (a search on copies), and talk to it as its run passes."""
    import collections
    machine = run.machine
    for _ in range(RAT_TRIGGER_STEPS):
        machine.run(4, "UP")
    root = 3901
    machine.mark(root)
    queue, seen, used, found = collections.deque([(root, ())]), set(), [], None
    try:
        while queue and len(used) < max_nodes and found is None:
            slot, path = queue.popleft()
            for way in ("UP", "RIGHT", "DOWN", "LEFT"):
                machine.restore(slot)
                machine.probe(2, "B+" + way)
                if run.dialog():
                    continue
                px, py = run.me()
                key = (int(px) // 4, int(py) // 4)
                if key in seen:
                    continue
                seen.add(key)
                if abs(px - RAT_INTERCEPT[0]) <= 6 and abs(py - RAT_INTERCEPT[1]) <= 6:
                    found = path + (way,)
                    break
                child = len(used) + 10
                machine.mark(child)
                used.append(child)
                queue.append((child, path + (way,)))
    finally:
        machine.restore(root)
        machine.free(root)
        for slot in used:
            machine.free(slot)
    assert found is not None, "a way to where the rat's run passes"
    for way in found:
        machine.run(2, "B+" + way)
    # The margin (issue #314): the rat runs west at about two pixels a frame
    # and stays in talking reach until it is TALK_REACH pixels past the
    # party's x. Measured in ct_native only (no recorded run of the original
    # catching the rat is in the library), so it is asserted, not confirmed:
    # the party arrives with about a frame to spare.
    px, rx = run.me()[0], run.object_pos(RAT)[0]
    margin = int((rx - (px - TALK_REACH)) // RAT_PIXELS_A_FRAME)
    run.say("rat margin", margin, "frames: party at", run.me(), "rat at", run.object_pos(RAT))
    assert margin >= RAT_MARGIN_FRAMES, ("the rat out of reach before the party arrives", margin)
    machine.run(1, "LEFT")
    for _ in range(60):
        machine.run(1, "A")
        machine.run(1)
        if run.dialog():
            break
    run.say("rat", "caught" if run.dialog() else "missed", "at", run.me(), "rat", run.object_pos(RAT))
    assert run.dialog(), "the rat caught"
    assert in_hand(run)


def password(run, door):
    """Hold L and R and confirm beside a sealed door, from below it. True once
    the door has answered (its flag in $7F:00A4 raised)."""
    machine = run.machine
    before = machine.b(DOORS_OPEN)
    tx, ty = run.object_pos(door)
    if run.walk(lambda m, px, py: abs(px - tx) <= 3 and abs(py - ty - 18) <= 3) is None:
        return False
    machine.run(2, "UP")
    machine.run(6)
    machine.run(10, "L+R")
    machine.run(4, "L+R+A")
    machine.run(10, "L+R")
    machine.run(60)
    assert in_hand(run, 20000)
    return machine.b(DOORS_OPEN) != before


def keep_the_mp(bot, state, rng, inventory):
    """The factory's fights: Robo fights as the general policy does, the
    others heal with Tonics and attack, keeping their MP for the R Series."""
    character = policies.party_ids(bot)[state.member]
    if character == ROBO:
        return policies.party(bot, state, rng, inventory)
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    for slot in members:
        if not state.hp(slot):
            action = policies.use(inventory, policies.REVIVE, slot)
            if action:
                return action
    lowest = min(members, key=lambda slot: policies.fraction(state, slot))
    if policies.fraction(state, lowest) < 0.5:
        action = policies.use(inventory, policies.TONIC, lowest)
        if action:
            return action
    living = policies.enemies(state)
    return ("attack", min(living, key=state.hp) if living else None)


def character_hp(machine, character):
    """A character's HP and max HP from their record."""
    at = CHARACTER_RECORDS + CHARACTER_RECORD_BYTES * character + RECORD_HP
    return machine.w(at), machine.w(at + 2)


def event_variable(machine, index):
    return machine.b(EVENT_VARIABLES + 2 * index)


def settle_the_scene(run, story, max_rounds=200):
    """Confirm the scene's windows, and leave the End of Time's exchange
    screen as it is, until the story has reached `story` and the pad walks
    the party again."""
    machine = run.machine
    for _ in range(max_rounds):
        if machine.b(MENU_SCREEN) == EXCHANGE_SCREEN and not run.dialog():
            machine.run(3, "B")
            machine.run(40)
        elif run.dialog():
            machine.run(3, "A")
            machine.run(40)
        else:
            machine.run(30)
        if machine.b(A.EVENT_MEMORY) >= story and machine.b(A.FIELD_PLAYER_CONTROL) and not run.dialog():
            return in_hand(run)
    return False


def talk_through(run, obj, story):
    """Speak to `obj` and play the scene out until the story reaches `story`."""
    run.talk(obj, done=lambda: run.dialog() or run.machine.b(A.EVENT_MEMORY) >= story)
    return settle_the_scene(run, story)


def pass_a_post(run, post):
    """Walk the leader into one of Spekkio's lap posts so that it counts once.
    A post counts again each time its count runs while the leader is still up
    against it, which ends the lap as missed; so the walk ends on the touch
    (or just after its count, before a second), the door's post from above,
    and the party then stands still until the post's count has run."""
    machine = run.machine
    level = 0x7E1C00 + 2 * post
    counts = LAP_COUNTS + (LAPS_DONE,)
    now = lambda m: [m.b(EVENT_VARIABLES + 2 * i) for i in counts]
    before = now(machine)
    from_above = (lambda py: py <= 214) if post == DOOR_POST else (lambda py: True)
    moves = run.walk_here(lambda m, px, py: from_above(py) and (
        (m.b(level) == TOUCHING and now(m) == before)
        or (now(m) != before and m.b(level) != TOUCHING and m.b(EVENT_VARIABLES + 2 * LAPS_DONE) != CHEATED)),
        max_nodes=8000)
    assert moves is not None, ("past the lap post", post)
    machine.run(40)
    run.say("lap post", post, before, "->", now(machine))
    assert event_variable(machine, LAPS_DONE) != CHEATED, ("the lap post counted twice", post)


def rest_with_a_shelter(run):
    """On a save point: a Shelter from the items screen restores everyone."""
    from .menu_sweep import ITEMS, back_to_main_menu, open_menu, open_option, press, to_item
    machine = run.machine
    had = count_of(machine, SHELTER)
    assert had, "a Shelter"
    open_menu(machine)
    open_option(machine, ITEMS)
    machine.run(20)
    to_item(run, SHELTER)
    for _ in range(6):
        press(machine, "A", 60)
        if count_of(machine, SHELTER) < had:
            break
    back_to_main_menu(machine)
    press(machine, "B", 60)
    assert count_of(machine, SHELTER) < had, "a Shelter used"
    assert run.wait_control(), "the field after the rest"


def dialogs_until_in_hand(run, where, max_rounds=200):
    """Confirm windows until the party stands in hand on map `where`."""
    machine = run.machine
    for _ in range(max_rounds):
        if run.map_id() == where and machine.b(A.FIELD_PLAYER_CONTROL) and not run.dialog():
            return in_hand(run)
        if run.dialog():
            machine.run(3, "A")
            machine.run(30)
        else:
            machine.run(30)
    return False


def rest_in_the_enertron(run):
    """Rest in Proto Dome's Enertron: every member's HP and MP restored."""
    machine = run.machine
    run.talk(ENERTRON, done=lambda: run.dialog())
    for _ in range(10):
        machine.run(3, "A")
        machine.run(50)
    assert in_hand(run, 6000)
    party = [c for c in machine.peek(A.PARTY_MEMBERS, 3) if c < 0x80]
    at = lambda c: CHARACTER_RECORDS + CHARACTER_RECORD_BYTES * c + RECORD_HP
    rested = all(machine.w(at(c)) == machine.w(at(c) + 2) and machine.w(at(c) + 4) == machine.w(at(c) + 6)
                 for c in party)
    run.say("rested", [character_hp(machine, c) for c in party])
    assert rested, "HP and MP restored"


def drink_tonics(run, margin=40):
    """From the main menu's items, Tonics for each member of the party short
    of their max HP by more than `margin`, while there are Tonics."""
    from .menu_sweep import ITEMS, back_to_main_menu, open_menu, open_option, press, use_item
    machine = run.machine
    party = list(machine.peek(A.PARTY_MEMBERS, 3))
    open_menu(machine)
    open_option(machine, ITEMS)
    machine.run(20)
    for row, character in enumerate(party):
        if character >= 0x80:
            continue
        for _ in range(12):
            hp, most = character_hp(machine, character)
            if most - hp <= margin or not count_of(machine, policies.TONIC):
                break
            use_item(run, policies.TONIC, choose_member=True, extra=("DOWN",) * row)
    back_to_main_menu(machine)
    press(machine, "B", 60)
    run.say("tonics drunk", [character_hp(machine, c) for c in party if c < 0x80],
            "left", count_of(machine, policies.TONIC))
    assert run.wait_control(), "the field after the menu"


def in_hand(run, max_frames=12000, seed=1, policy=early.early_party):
    """Play on until the pad walks the party again: windows closed with
    confirm, each name the game asks for accepted as offered (Start), the
    party exchange a joining member opens left as offered (cancel), and any
    fight that starts on the way won. True once it is."""
    machine = run.machine
    start = machine.frame
    found = held = 0
    while machine.frame - start < max_frames:
        if early.battle_ready(run):
            win(run, seed, policy)
            found = held = 0
            start = machine.frame     # the wait is for the field: a long fight's own limit is win's
            continue
        if naming(run):
            machine.run(3, "START")
            machine.run(40)
            continue
        if machine.b(MENU_SCREEN) == EXCHANGE_SCREEN and not machine.b(A.FIELD_PLAYER_CONTROL):
            machine.run(3, "B")
            machine.run(90)
            found = held = 0
            continue
        if run.dialog():
            machine.run(3, "A")
            machine.run(37)
            found = held = 0
            continue
        # The field gives the pad back. find_player's probe finds the party
        # moving; one hemmed in on both sides of an axis doesn't pass it, so
        # the field's control held for a while will do too.
        if machine.b(A.FIELD_PLAYER_CONTROL):
            if run.find_player() is not None:
                found += 1
                if found >= 4:
                    return True
            else:
                held += 1
                if held >= 8:
                    return True
        else:
            found = held = 0
        machine.run(30)
    return False


def without(item, policy):
    """`policy` with `item` kept back in the bag."""
    def held_back(bot, state, rng, inventory):
        items, counts = inventory
        counts = [0 if kind == item else count for kind, count in zip(items, counts)]
        return policy(bot, state, rng, (items, counts))
    return held_back


# The mountains' fights heal with Tonics, keeping the Mid Tonics for Masa and Mune.
MOUNTAIN_FIGHTS = without(policies.MID_TONIC, early.early_party)


def climb(run, want, aim):
    """To map `want` in the Denadoro Mountains: the shortest walk where there is
    one, or else the search that plays ladders and falls through, aimed at `aim`
    (leg B's search_walk); fights on the way are run from or won with Tonics."""
    for _ in range(4):
        if run.map_id() == want:
            break
        assert in_hand(run, policy=MOUNTAIN_FIGHTS), "the field in hand"
        walking(run)
        if run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == want, max_nodes=6000) is None:
            LEG_B.search_walk(run, want, aim, policy=MOUNTAIN_FIGHTS)
    assert in_hand(run, policy=MOUNTAIN_FIGHTS), "the field in hand"
    run.say("climbed", want, run.me(), "story", hex(run.machine.b(A.EVENT_MEMORY)))
    assert run.map_id() == want, want


def levels(machine):
    """The active party's levels."""
    return [machine.b(CHARACTER_RECORDS + CHARACTER_RECORD_BYTES * c + RECORD_LEVEL)
            for c in machine.peek(A.PARTY_MEMBERS, 3) if c < 0x80]


def train(run, level, away, back, max_fights=200, policy=MOUNTAIN_FIGHTS):
    """Fight the monsters about two neighbouring maps until every member of
    the party is at `level`: each fight the walk finds is won with `policy`,
    and when none is left on one map the party steps through to the other
    (`away` and `back`, each a map and its way in, as climb takes them); the
    monsters of the map it left are there again when it comes back."""
    machine = run.machine
    fights = crossings = 0
    while min(levels(machine)) < level:
        assert fights < max_fights and crossings < 2 * max_fights, (
            "too long a training for level", level, levels(machine), fights, crossings)
        walking(run)
        moves = run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN), max_nodes=4000)
        if moves is not None:
            machine.run(30)
            if run.dialogs_until(lambda: early.battle_ready(run) or bool(machine.b(A.FIELD_PLAYER_CONTROL)), 3000)                     and early.battle_ready(run):
                win(run, policy=policy)
                fights += 1
                assert in_hand(run, policy=policy), "the field after the fight"
                continue
        climb(run, *(away if run.map_id() == back[0] else back))
        crossings += 1
    if run.map_id() != back[0]:
        climb(run, *back)
    run.say("trained", fights, "fights", crossings, "crossings: levels", levels(machine))


def speak(run, obj, tries=3):
    """Speak to object `obj` from beside it, below first: walked to, turned to
    it and confirmed. True once a window opens."""
    machine = run.machine
    tx, ty = run.object_pos(obj)
    for dx, dy, face in ((0, 18, "UP"), (-18, 0, "RIGHT"), (18, 0, "LEFT"), (0, -18, "DOWN")):
        if run.walk_here(lambda m, x, y: abs(x - tx - dx) <= 4 and abs(y - ty - dy) <= 4, max_nodes=2000) is None:
            continue
        machine.run(3, face)
        machine.run(5)
        for _ in range(tries):
            machine.run(3, "A")
            machine.run(30)
            if run.dialog():
                return True
    return False


def read_through(run, max_windows=60):
    """Confirm the windows that are open until none is."""
    machine = run.machine
    for _ in range(max_windows):
        if not run.dialog():
            return
        machine.run(3, "A")
        machine.run(30)


def masa_and_mune(bot, state, rng, inventory):
    """Masa and Mune, and the two as one: Marle's Ice and Lucca's Fire, which hurt
    them far more than blows, on the weaker; against the two as one Crono's
    Slash each turn, which undoes the Tornado energy they store up for a blow
    at everyone (rpgclassics.com's and gamercorner.net's boss guides: "have
    Crono use the Slash tech to neutralise it"), his magic kept for it by
    blows against the two apart; Marle keeps the party up, with Aura for one
    not yet low and Mid Tonics, and the fallen are raised with Revives."""
    member = state.member
    character = policies.party_ids(bot)[member]
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    for slot in members:
        if not state.hp(slot):
            action = policies.use(inventory, policies.REVIVE, slot)
            if action:
                return action
    living = [slot for slot in members if state.hp(slot)]
    lowest = min(living, key=lambda slot: policies.fraction(state, slot))
    hurt = policies.fraction(state, lowest) < 0.5
    if hurt and (character == MARLE or policies.fraction(state, lowest) < 0.25):
        if (character == MARLE and policies.fraction(state, lowest) > 0.25 and state.mp(member) >= AURA_MP
                and policies.tech_known(bot, member, AURA)):
            return "tech", AURA, lowest
        for item in (policies.MID_TONIC, policies.TONIC):
            action = policies.use(inventory, item, lowest)
            if action:
                return action
    foes = policies.enemies(state)
    if not foes:
        return "attack", None
    target = min(foes, key=state.hp)
    as_one = any(state.max_hp(slot) >= MASA_AND_MUNE_AS_ONE_HP for slot in foes)
    if as_one and character == CRONO and state.mp(member) >= SLASH_MP and policies.tech_known(bot, member, SLASH):
        return "tech", SLASH, target
    if character == MARLE and state.mp(member) >= ICE_MP:
        return "tech", ICE, target
    if character == LUCCA and state.mp(member) >= FIRE_MP:
        return "tech", FIRE, target
    return "attack", target


def walking(run, max_rounds=8):
    """in_hand, and the walker set to the party's leader. find_player's probe
    finds the leader moving; one hemmed in (the soldiers round the party in
    Guardia Forest) doesn't pass it, and the field's own record of the
    party's objects names it instead."""
    for _ in range(max_rounds):
        assert in_hand(run), "the field in hand"
        if run.find_player() is not None:
            return
        run.machine.run(30)
    leader = run.machine.w(A.FIELD_PARTY_OBJECTS)
    assert leader < 0x80, "a leader on the field"
    run.player = leader // 2
    run.say("the leader is object", run.player)


def leave_fighting(run, want, max_rounds=12, seed=1, ends=(), policy=early.early_party):
    """Walk until the screen is map `want`, winning the fights on the way (the
    field's encounters take control from the walk) and closing windows. A
    scene that carries the party on to one of the maps in `ends` ends the
    walk there too."""
    machine = run.machine
    done = lambda: run.map_id() == want or run.map_id() in ends
    for _ in range(max_rounds):
        assert in_hand(run, policy=policy), "the field in hand"
        if done():
            return True
        moves = run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == want
                         or not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN))
        run.say("leave_fighting", want, "steps", moves and len(moves), "now", run.map_id(), "at", run.me())
        if done():
            return True
        if moves is None:
            return False
        machine.run(30)
        if early.battle_ready(run) or run.dialogs_until(lambda: early.battle_ready(run) or bool(
                machine.b(A.FIELD_PLAYER_CONTROL)), 3000) and early.battle_ready(run):
            win(run, seed, policy)
    return done()


def through(run, x, y, arrived, seed=1, rounds=10, limit=1800, policy=early.early_party):
    """Head for (x, y) by the biased search (_trial_walk.toward, searching up
    to `limit` places) until `arrived()`, settling the fights and scenes that
    stop the walk."""
    machine = run.machine
    for _ in range(rounds):
        assert in_hand(run, policy=policy), "the field in hand"
        if arrived():
            return
        toward(run, x, y, lambda: arrived() or not machine.b(A.FIELD_PLAYER_CONTROL) or run.dialog(),
               limit=limit)
        if arrived():
            return
        assert in_hand(run, 30000, seed, policy), "the scene or fight ends"
    raise AssertionError(("too many stops on the way to", (x, y)))


def gold(machine):
    return machine.b(GOLD) | machine.b(GOLD + 1) << 8 | machine.b(GOLD + 2) << 16


def buy(run, merchant_spot, purchases):
    """At a shop's counter: speak to the merchant from `merchant_spot` (below
    the counter, facing it), and buy each (row of the shop's list, how many,
    the price), as many as the party's gold pays for: the count starts at
    one, Up adds ten and Right one. The shop's screens are told apart by the
    menus' cursor: the Buy/Sell row (8), the list (the row) and the count
    (10)."""
    machine = run.machine
    assert in_hand(run)
    assert run.go(*merchant_spot, tolerance=3), "at the counter"
    machine.run(2, "UP")
    machine.run(8)

    def press_until(button, done, tries=4):
        for _ in range(tries):
            machine.run(3, button)
            machine.run(60)
            if done():
                return True
        return False

    cursor = lambda: machine.b(CURSOR)
    assert press_until("A", lambda: machine.b(MENU_SCREEN) == SHOP_SCREEN and cursor() == SHOP_BUY, 10), "the shop opens"
    assert press_until("A", lambda: cursor() == 0), "its list"
    for row, count, price in purchases:
        count = min(count, gold(machine) // price)
        if count == 0:
            continue
        for _ in range(12):
            if cursor() == row:
                break
            machine.run(3, "DOWN" if cursor() < row else "UP")
            machine.run(30)
        assert cursor() == row, "the list's row %d" % row
        assert press_until("A", lambda: cursor() == SHOP_COUNT), "the count"
        for _ in range((count - 1) // 10):
            machine.run(3, "UP")
            machine.run(20)
        for _ in range((count - 1) % 10):
            machine.run(3, "RIGHT")
            machine.run(20)
        assert press_until("A", lambda: cursor() == row), "bought"
    # Out of the shop's screens: the field's control byte can read set while
    # they are open (Arris Dome's), and the menu's screen byte keeps the shop's
    # after they close, so the pad walking the party again tells.
    for _ in range(6):
        if run.dialog() or (machine.b(A.FIELD_PLAYER_CONTROL) and run.find_player() is not None):
            break
        machine.run(3, "B")
        machine.run(60)
    assert in_hand(run)


def shop_at_home(run):
    """Back in 1000 A.D. before the trial: Tonics and Revives from Truce's
    market for the prison and the Dragon Tank after it."""
    machine = run.machine
    run.wait_world_control()
    world_enter(run, TRUCE_MARKET)
    buy(run, TRUCE_MARKET_COUNTER, [(TRUCE_TONIC_ROW, 60, 10), (TRUCE_REVIVE_ROW, 2, 200)])
    run.say("supplies", "tonics", count_of(machine, policies.TONIC), "revives", count_of(machine, policies.REVIVE))
    assert leave_fighting(run, "f001"), "out of the market"
    run.wait_world_control()


def party_size(run):
    return sum(1 for i in range(3) if run.machine.b(A.PARTY_MEMBERS + i) < 0x80)


def new_game(run):
    """Power on with no game saved: the title's Start goes straight to the
    battle mode question; Wait, then Crono's name as offered."""
    machine = run.machine
    machine.run(1620)
    machine.run(5, "START")
    for _ in range(60):
        machine.run(10)
        if machine.b(MENU_SCREEN) == NAMING_SCREEN:
            break
    assert machine.b(MENU_SCREEN) == NAMING_SCREEN, "the battle mode question"
    machine.run(40)
    machine.run(3, "DOWN")
    machine.run(20)
    assert machine.b(CURSOR) == WAIT_MODE, "Wait chosen"
    machine.run(3, "A")
    machine.run(60)
    for _ in range(40):
        if run.map_id() not in ("0000", "b101"):
            break
        machine.run(3, "START")
        machine.run(30)
    # The opening flies over the kingdom with the field's control byte set
    # and no one to walk: in_hand would take that for the party in hand.
    for _ in range(200):
        if run.map_id() != OPENING_FLYOVER:
            break
        machine.run(30)
    assert run.map_id() != OPENING_FLYOVER, "the opening's flight over the kingdom"
    assert in_hand(run, 20000), "Crono awake in his room"
    assert run.map_id() == CRONOS_ROOM, "Crono's room"
    run.say("Crono's room", run.map_id(), "story", hex(machine.b(A.EVENT_MEMORY)))


def main():
    args = route_arguments(__doc__.splitlines()[0], blank=True)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            new_game(run)
            run.checkpoint("named")
        if start <= 1:
            # Downstairs Crono's mother asks Lucca's name, and the house's door
            # leads out to the 1000 A.D. world map.
            assert run.exit_to("0100"), "downstairs"
            assert in_hand(run)
            run.talk(15, done=lambda: naming(run))   # Lucca's name: in_hand below checks it was given
            assert in_hand(run)
            assert run.exit_other({"0200"}), "out of the house"
            assert run.map_id() == "f001", "the 1000 A.D. world map"
            run.checkpoint("home")
        if start <= 2:
            world_enter(run, LEENE_SQUARE)
            assert in_hand(run)
            run.checkpoint("fair")
        if start <= 3:
            # Marle runs into Crono on his way up to the bell and drops her
            # pendant.
            assert run.exit_to("b701"), "the bell's plaza"
            moves = run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL) or y < 150)
            run.say("toward the bell", moves and len(moves), run.me(), "control", machine.b(A.FIELD_PLAYER_CONTROL))
            assert in_hand(run)
            story = lambda: machine.b(A.EVENT_MEMORY)
            run.say("collided", run.me(), "story", hex(story()))
            run.checkpoint("collided")
        if start <= 4:
            # She paces about looking for the pendant, which lies at the foot
            # of the stall beside the bell: Crono picks it up, from below it,
            # hands it back, and she joins him.
            story = lambda: machine.b(A.EVENT_MEMORY)
            found = lambda: machine.b(FAIR_FLAGS) & PENDANT_FOUND
            assert run.go(488, 162, tolerance=3), "below the pendant"
            for _ in range(6):
                machine.run(2, "UP")
                machine.run(8)
                machine.run(3, "A")
                machine.run(47)
                if found():
                    break
            assert found(), "the pendant picked up"
            assert in_hand(run)
            joined = lambda: machine.b(A.PARTY_MEMBERS + 1) < 0x80
            assert run.talk(MARLE_AT_THE_BELL), "the pendant handed back"
            assert in_hand(run)
            # Walking on, Crono is stopped: she asks to come along, and the
            # game asks her name.
            for _ in range(4):
                if joined():
                    break
                early.advance(run, "UP", lambda: not machine.b(A.FIELD_PLAYER_CONTROL) or run.dialog())
                assert in_hand(run)
            assert joined(), "Marle joins"
            run.say("Marle", hex(story()), "party", machine.peek(A.PARTY_MEMBERS, 3).hex())
            machine.screenshot(run.prefix + "_plaza.png")
            run.checkpoint("joined")
        if start <= 5:
            # Two fairgoers stand on the steps up to Lucca's exhibit until
            # someone in the lower square has told Crono that her show has
            # started ($7F:0055 bit 7; the steps' two read it as the plaza
            # loads).
            told = lambda: machine.b(FAIR_NEWS) & SHOW_STARTED
            assert run.exit_to("0500"), "back to the lower square"
            for fairgoer in LOWER_SQUARE_FAIRGOERS:
                if told():
                    break
                run.talk(fairgoer, done=told)
            assert told(), "Lucca's show has started"
            assert run.exit_to("b701"), "the bell's plaza again"
            assert run.exit_to("0800"), "Lucca's exhibit"
            assert in_hand(run)
            run.checkpoint("exhibit")
        if start <= 6:
            # Lucca, on the left telepod, sends Crono across to the right one
            # when he speaks to her from below it; then Marle goes, and her
            # pendant opens a Gate that takes her.
            story = lambda: machine.b(A.EVENT_MEMORY)
            assert in_hand(run)
            assert run.go(62, 116, tolerance=4), "below the left telepod"
            machine.run(20, "UP")
            for _ in range(6):
                machine.run(3, "A")
                machine.run(40)
                if run.dialog():
                    break
            assert run.dialogs_until(lambda: story() >= 0x0A, 6000), "the demonstration"
            assert in_hand(run)
            run.talk(MARLE_AT_THE_EXHIBIT, done=lambda: story() >= 0x0C)
            assert in_hand(run)
            assert story() >= 0x0C, "Marle gone through the Gate"
            # Crono steps onto the left telepod after her, with her pendant,
            # and the Gate opens again for him.
            assert run.go(62, 116, tolerance=4), "below the left telepod"
            machine.run(20, "UP")
            assert run.dialogs_until(lambda: run.map_id() != "0800", 20000), "through the Gate"
            assert run.map_id() == TRUCE_CANYON_600, "600 A.D."
            assert in_hand(run, 20000)
            run.checkpoint("gate")
        if start <= 7:
            # Truce Canyon to the 600 A.D. world map, Crono alone.
            assert leave_fighting(run, "7000"), "the canyon's lower part"
            assert leave_fighting(run, "f101"), "the 600 A.D. world map"
            run.checkpoint("truce_canyon")
        if start <= 8:
            # Through Guardia Forest, south to north, to the castle.
            run.wait_world_control()
            world_enter(run, GUARDIA_FOREST_600)
            through(run, 296, 24, lambda: run.map_id() == "f101")
            run.wait_world_control()
            world_enter(run, GUARDIA_CASTLE_600)
            assert in_hand(run)
            run.say("castle", run.map_id(), run.me(), "story", hex(machine.b(A.EVENT_MEMORY)))
            run.checkpoint("guardia")
        if start <= 9:
            # Up the spiral stair to the queen's chamber (the way cronos_trial
            # goes, as Leene is restored), where Marle, taken for the queen,
            # vanishes, and Lucca arrives.
            story = lambda: machine.b(A.EVENT_MEMORY)
            assert in_hand(run)
            through(run, 616, 136, lambda: run.map_id() == "d401")
            cronos_trial.spiral(run, "RIGHT", "7a00")
            assert in_hand(run)
            run.say("chamber", run.me(), "story", hex(story()))
            run.checkpoint("upstairs")
        if start <= 10:
            # The chancellor stands in the doorway down to the queen's
            # chamber and steps aside once spoken to.
            story = lambda: machine.b(A.EVENT_MEMORY)
            assert in_hand(run)
            assert run.talk(CHANCELLOR), "the chancellor"
            assert in_hand(run)
            through(run, 648, 696, lambda: story() > 0x0F)
            assert in_hand(run, 20000)
            run.say("after", run.map_id(), run.me(), "story", hex(story()), "party", machine.peek(A.PARTY_MEMBERS, 3).hex())
            run.checkpoint("chamber")
        if start <= 11:
            # Lucca comes, and the two leave the castle for the Cathedral.
            story = lambda: machine.b(A.EVENT_MEMORY)
            assert in_hand(run)
            run.say("Lucca?", hex(story()), "party", machine.peek(A.PARTY_MEMBERS, 3).hex(), run.me())
            through(run, 872, 728, lambda: run.map_id() == "d401")
            cronos_trial.spiral(run, "LEFT", "7800")
            assert in_hand(run)
            through(run, 504, 888, lambda: run.map_id() == "f101")
            run.say("out", hex(story()), "party", machine.peek(A.PARTY_MEMBERS, 3).hex(), run.world_pos())
            run.checkpoint("kidnapped")
        if start <= len(OPENING):
            # Back south through the forest to where the Cathedral's save
            # stands, beside its door (the nearest door is the one its piece
            # takes).
            run.wait_world_control()
            world_enter(run, GUARDIA_FOREST_600, row=15)
            through(run, 296, 760, lambda: run.map_id() == "f101" and run.me()[1] > 700)
            # Supplies from the village's shop by the canyon: the bot heals
            # with Tonics and raises the fallen with Revives, and a Shelter
            # at a save point restores the whole party.
            assert run.world_go(*TRUCE_600), "the village"
            world_enter(run, TRUCE_600_SHOP)
            buy(run, SHOP_COUNTER, [(TONIC_ROW, 40, 10), (REVIVE_ROW, 1, 200), (SHELTER_ROW, 2, 150)])
            run.say("supplies", "tonics", count_of(machine, policies.TONIC), "revives", count_of(machine, policies.REVIVE),
                    "shelters", count_of(machine, SHELTER))
            assert leave_fighting(run, "f101"), "out of the shop"
            run.wait_world_control()
            assert run.world_go(*CATHEDRAL_SAVE_SPOT), "beside the Cathedral"
        if start < len(OPENING) + len(CATHEDRAL):
            cathedral.play(run, max(1, start - len(OPENING) + 1), args.seed)
        if start < len(OPENING) + len(CATHEDRAL) + len(cronos_trial.PHASES):
            cronos_trial.play(run, max(0, start - len(OPENING) - len(CATHEDRAL)), args.seed, errand=shop_at_home)
        later = start - len(OPENING) - len(CATHEDRAL) - len(cronos_trial.PHASES)
        if later <= 0:
            # Three days in the cell, the march to the guillotine and Lucca's
            # rescue; then out through the prison.
            story = lambda: machine.b(A.EVENT_MEMORY)
            assert run.dialogs_until(lambda: story() >= 0x2E and machine.b(A.FIELD_PLAYER_CONTROL), 30000), "Lucca's rescue"
            assert in_hand(run)
            run.say("rescued", run.map_id(), run.me(), "party", machine.peek(A.PARTY_MEMBERS, 3).hex())
            run.checkpoint("rescued")
        if later <= 1:
            assert in_hand(run)
            # The prison's exits (from the location's exit records): the
            # execution room's door to the right tower; down it to the lower
            # bridge (1c00); across to the left tower, past its guards, and
            # up its stair (an exit to its own map) to the room by the upper
            # bridge (1d00), where the escape's save is, and out onto it.
            assert leave_fighting(run, "4800"), "the right tower"
            assert leave_fighting(run, "1c00"), "the lower bridge"
            through(run, 32, 408, lambda: run.map_id() == "4800")
            through(run, 400, 470, lambda: run.map_id() == "4800" and run.me()[0] > 850)
            through(run, 880, 88, lambda: run.map_id() == "1d00")
            through(run, 80, 88, lambda: run.map_id() == "1c00")   # out on the upper bridge
            assert in_hand(run)
            run.checkpoint("escaped")
        if later <= len(ESCAPE) + len(TANK) - 1:
            dragon_tank.play(run, max(2, later - len(ESCAPE) + 2), args.seed)
        later -= len(ESCAPE) + len(TANK)
        if later <= 0:
            # Left along the bridge to the castle's front. The chancellor's
            # taunt opens again while the party stands where it is spoken,
            # so the party walks on through it.
            for _ in range(400):
                if run.map_id() == GUARDIA_CASTLE_1000:
                    break
                if run.dialog():
                    machine.run(3, "A")
                    machine.run(5, "LEFT")
                else:
                    machine.run(8, "LEFT")
            assert run.map_id() == GUARDIA_CASTLE_1000, "the castle's front hall"
            assert in_hand(run)
            run.checkpoint("castle_gate")
        if later <= 1:
            # Out of the door, Marle runs to join them, and the guards chase
            # the three into Guardia Forest.
            assert leave_fighting(run, "f001", ends=(GUARDIA_FOREST_1000,)), "chased out"
            assert run.map_id() == GUARDIA_FOREST_1000, "into the forest"
            assert in_hand(run)
            run.say("chased", run.me(), "story", hex(machine.b(A.EVENT_MEMORY)),
                    "party", machine.peek(A.PARTY_MEMBERS, 3).hex())
            run.checkpoint("forest_chase")
        if later <= 2:
            # Soldiers close the way back west ("There's no escape!" each
            # time the party turns to them): on into the forest's back. The
            # walk ends at a window only as a dead end, not as a goal.
            walking(run)
            moves = run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() != GUARDIA_FOREST_1000,
                             max_nodes=6000)
            assert moves is not None and run.map_id() == FOREST_GATE, "the forest's back"
            assert in_hand(run)
            run.checkpoint("forest_back")
        if later <= 3:
            # Cornered, Lucca finds the Gate: confirming beside it takes the
            # three to 2300 A.D., in Bangor Dome.
            walking(run)
            assert run.talk(GATE_IN_THE_FOREST, done=lambda: run.map_id() != FOREST_GATE
                            or not machine.b(A.FIELD_PLAYER_CONTROL)), "the Gate"
            assert in_hand(run, 30000)
            assert run.map_id() == BANGOR_DOME, "2300 A.D."
            run.checkpoint("future")
        if later <= 4:
            # Out onto the 2300 A.D. map and north into Lab 16's ruins.
            assert leave_fighting(run, WORLD_2300), "out of Bangor Dome"
            assert run.world_go(*LAB16_DOOR), "Lab 16's door"
            machine.run(3, "A")
            machine.run(90)
            assert run.map_id() == LAB16_WEST, "Lab 16"
            assert in_hand(run)
            run.checkpoint("lab16")
        if later <= 5:
            # Across the ruins (their Shadows, which no weapon hurts, run
            # from) and out by Arris Dome.
            through(run, *LAB16_WEST_EXIT, lambda: run.map_id() == LAB16_EAST, rounds=20)
            through(run, *LAB16_EAST_EXIT, lambda: run.map_id() == WORLD_2300, rounds=20)
            machine.screenshot(run.prefix + "_lab16.png")
            run.checkpoint("lab16_east")
        if later <= 6:
            run.wait_world_control()
            world_enter(run, ARRIS_DOME)
            assert in_hand(run, 20000)
            assert run.map_id() == guardian.HALL, "Arris Dome"
            run.say("Arris Dome", run.me(), "story", hex(machine.b(A.EVENT_MEMORY)))
            run.checkpoint("arris")
        if later <= len(FUTURE) + len(GUARDIAN) - 1:
            walking(run)
            guardian.play(run, max(1, later - len(FUTURE) + 1), args.seed)
        later -= len(FUTURE) + len(GUARDIAN)
        if later <= 0:
            # The door the Guardian kept opens to the information center,
            # where the party finds a Seed as it comes in.
            walking(run)
            assert run.go(*INFO_CENTER_DOOR), "below the door"
            machine.run(4, "UP")
            machine.run(10)
            machine.run(4, "A")
            machine.run(60)
            assert leave_fighting(run, INFO_CENTER), "the information center"
            assert in_hand(run)
            assert count_of(machine, SEED), "the Seed"
            run.checkpoint("info_center")
        if later <= 1:
            # Its computer shows the Day of Lavos.
            walking(run)
            assert run.talk(COMPUTER), "the computer"
            assert in_hand(run, 30000)
            assert machine.b(ARRIS_FLAGS) & COMPUTER_SEEN, "the Day of Lavos seen"
            run.checkpoint("lavos_day")
        later -= len(INFO)
        if later <= 0:
            # The rat in the room below knows the password. Each of its two
            # rats runs the length of the room from one end to the other
            # when the party steps near, through pipes the party can't
            # follow; the party catches it by waiting where its run passes
            # and talking to it as it comes. From the room's foot (in by the
            # inner room) the run turns north along the west pipe, which the
            # party reaches by a dash a few frames before the rat.
            walking(run)
            for want in (GUARDIAN_ROOM, DOME_LOWER, DOME_INNER, DOME_LOWER):
                assert leave_fighting(run, want), want
            walking(run)
            catch_the_rat(run)
            assert machine.b(ARRIS_FLAGS) & RAT_TOLD, "the password"
            run.checkpoint("rat")
        if later <= 1:
            # Hold L and R and confirm at the inner room's sealed door.
            assert leave_fighting(run, DOME_INNER), "the inner room"
            walking(run)
            assert password(run, INNER_DOOR), "the inner room's door open"
            run.checkpoint("sealed_door")
        if later <= 2:
            # Beyond it, the passage and the store hall, its own sealed door,
            # and the storeroom: walking in, the party finds the food gone.
            assert leave_fighting(run, PASSAGE), "the passage"
            through(run, 408, 36, lambda: run.map_id() == STORE_HALL, rounds=20)
            walking(run)
            assert password(run, STORE_DOOR), "the store hall's door open"
            assert leave_fighting(run, STOREROOM), "the storeroom"
            for _ in range(60):
                if machine.b(A.EVENT_MEMORY) >= STORY_STORE_SEEN or not machine.b(A.FIELD_PLAYER_CONTROL):
                    break
                machine.run(4, "UP")
            assert in_hand(run, 30000)
            assert machine.b(A.EVENT_MEMORY) == STORY_STORE_SEEN, "the storeroom's scene"
            run.checkpoint("storeroom")
        if later <= 3:
            # Back to the hall, where Doan waits by the ladder: the Seed for
            # the Bike Key as the party steps off it.
            for want in (STORE_HALL, PASSAGE, DOME_INNER, DOME_HALL):
                assert leave_fighting(run, want), want
            for _ in range(8):
                if machine.b(A.EVENT_MEMORY) >= STORY_BIKE_KEY:
                    break
                machine.run(16, "UP")
                assert in_hand(run, 30000)
            assert machine.b(A.EVENT_MEMORY) == STORY_BIKE_KEY and count_of(machine, BIKE_KEY), "the Bike Key"
            run.checkpoint("bike_key")
        later -= len(KEY)
        if later <= 0:
            # Tonics and Revives from the dome's merchant for the factory,
            # then out of the dome and north to Lab 32's west end.
            walking(run)
            mx, my = run.object_pos(ARRIS_MERCHANT)
            buy(run, (mx, my + ARRIS_COUNTER_BELOW), [(ARRIS_TONIC_ROW, 40, 10), (ARRIS_REVIVE_ROW, 4, 200)])
            assert leave_fighting(run, WORLD_2300), "out of Arris Dome"
            run.wait_world_control()
            world_enter(run, LAB32_ENTRANCE)
            assert run.map_id() == LAB32_WEST, "Lab 32"
            assert in_hand(run)
            run.checkpoint("lab32")
        if later <= 1:
            # Johnny's gang closes in as the party looks at the jet bike, and
            # Johnny, spoken to, explains its boosts.
            walking(run)
            assert run.go(*BIKE_APPROACH, tolerance=3), "toward the jet bike"
            machine.run(10, "UP")
            machine.run(9, "UP+A")
            machine.run(30)
            spoken = False
            for _ in range(800):
                machine.run(3, "A")
                machine.run(9)
                spoken = spoken or run.dialog()
                if spoken and machine.b(A.FIELD_PLAYER_CONTROL) and not run.dialog() and run.find_player() is not None:
                    break
            assert spoken, "Johnny's gang"
            assert run.talk(JOHNNY), "Johnny"
            assert in_hand(run)
            run.checkpoint("johnny")
        if later <= 2:
            # The race down Lab 32 to its east end, steered with the winning
            # run's input (as proto_dome's own approach does), from the frame
            # the race begins.
            from .. import mkmovie
            walking(run)
            assert run.go(*JET_BIKE_SPOT, tolerance=2), "below the jet bike"
            machine.run(10, "UP")
            machine.run(4, "UP+A")
            for _ in range(60):
                if run.map_id() == RACE:
                    break
                machine.run(1)
            assert run.map_id() == RACE, "the race"
            pads = mkmovie.load_movie_pads(os.path.join(REPO, RACE_MOVIE), RACE_ENDS)
            for frame in range(RACE_STARTS, RACE_ENDS):
                if run.map_id() != RACE:
                    break
                machine.run(1, pads[frame])
            assert run.map_id() == LAB32_EAST, "Lab 32's east end"
            assert in_hand(run)
            run.checkpoint("raced")
        later -= len(LAB32)
        if later <= len(PROTO) - 1:
            walking(run)
            proto_dome.play(run, max(1, later + 1), args.seed)
        later -= len(PROTO)
        if later <= 0:
            # A rest in Proto Dome's Enertron for HP and MP, then Robo leads
            # the way to the factory across the dome's plain.
            walking(run)
            rest_in_the_enertron(run)
            assert leave_fighting(run, WORLD_2300), "out of Proto Dome"
            run.wait_world_control()
            world_enter(run, FACTORY_ENTRANCE)
            assert run.map_id() == r_series.HALL, "the factory's hall"
            assert run.wait_control(), "the hall in hand"
            run.checkpoint("factory_hall")
        if later <= len(FACTORY) - 1:
            walking(run)
            r_series.play(run, max(2, later + 1), args.seed, policy=keep_the_mp, before_the_r_series=drink_tonics)
        later -= len(FACTORY)
        if later <= 0:
            # Gaspar tells of the pillars of light, and that one of the four
            # must stay; the other keeper says what comes next.
            walking(run)
            assert talk_through(run, GASPAR, 0x49), "Gaspar"
            assert talk_through(run, GATE_KEEPER, 0x4A), "the End of Time's other keeper"
            run.checkpoint("gaspar")
        if later <= 1:
            # Gaspar again: Spekkio's door opens.
            walking(run)
            assert talk_through(run, GASPAR, STORY_SPEKKIO_DOOR), "Spekkio's door"
            assert run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == SPEKKIO_ROOM) is not None, "Spekkio's room"
            run.checkpoint("spekkio_door")
        if later <= 2:
            walking(run)
            assert talk_through(run, SPEKKIO, STORY_SPEKKIO_MET), "Spekkio"
            run.checkpoint("spekkio")
        if later <= 3:
            # Three laps of his room, clockwise along the wall from the
            # door, past each post once a lap.
            walking(run)
            for _ in range(3):
                for post in LAP_POSTS:
                    pass_a_post(run, post)
            pass_a_post(run, DOOR_POST)
            assert event_variable(machine, LAPS_DONE) == 3, "three laps"
            run.checkpoint("laps")
        if later <= 4:
            # Spoken to after the laps, he teaches the three their magic.
            assert run.walk_here(lambda m, px, py: abs(px - SPEKKIO_SPOT[0]) <= 4 and abs(py - SPEKKIO_SPOT[1]) <= 4
                                 and event_variable(m, LAPS_DONE) == 3) is not None, "before Spekkio"
            machine.run(2, "UP")
            machine.run(8)
            machine.run(3, "A")
            machine.run(40)
            assert settle_the_scene(run, STORY_MAGIC), "magic"
            assert leave_fighting(run, END_OF_TIME_HALL), "back to the End of Time"
            run.checkpoint("magic")
        later -= len(END_OF_TIME)
        if later <= len(CAVE) - 2:
            walking(run)
            heckran_prepare.play(run, max(1, later + 1), args.seed, save=False)
        if later <= len(CAVE) - 1:
            # The cave's save point: a Shelter restores everyone's HP and MP
            # before Heckran.
            walking(run)
            assert run.walk(lambda m, x, y: m.b(SAVE_POINT_FLAGS) & ON_A_SAVE_POINT) is not None, "the cave's save point"
            machine.run(30)
            rest_with_a_shelter(run)
            run.checkpoint("cave_rest")
        later -= len(CAVE)
        if later <= len(HECKRAN) - 1:
            walking(run)
            heckran.play(run, max(1, later + 1), args.seed, max_tries=600)
        later -= len(HECKRAN)
        if later <= 0:
            # The whirlpool in Heckran's chamber carries the party out to
            # the sea by Lucca's house.
            walking(run)
            assert run.map_id() == HECKRANS_CHAMBER
            run.talk(WHIRLPOOL, done=lambda: run.dialog())
            for _ in range(80):
                if run.map_id() == WORLD_1000 and machine.b(A.FIELD_PLAYER_CONTROL) and not run.dialog():
                    break
                machine.run(3, "A") if run.dialog() else machine.run(30)
            assert run.map_id() == WORLD_1000, "the 1000 A.D. world map"
            run.checkpoint("whirlpool")
        if later <= 1:
            # Leene Square's Gate, between Lucca's telepods, goes to the End
            # of Time now; going through it shows Truce Canyon's pillar there.
            run.wait_world_control()
            world_enter(run, LEENE_SQUARE)
            assert in_hand(run)
            assert run.exit_to("b701"), "the bell's plaza"
            assert run.exit_to(LEENE_EXHIBIT), "Lucca's exhibit"
            walking(run)
            assert run.walk(lambda m, px, py: abs(px - LEENE_GATE[0]) <= 6 and abs(py - LEENE_GATE[1]) <= 8) is not None
            for _ in range(10):
                if run.dialog() or run.map_id() != LEENE_EXHIBIT:
                    break
                machine.run(4, "UP")
                machine.run(3, "A")
                machine.run(30)
            assert dialogs_until_in_hand(run, END_OF_TIME_HALL), "the End of Time"
            run.checkpoint("leene_gate")
        if later <= 2:
            # Truce Canyon's pillar, stepped onto and confirmed.
            walking(run)
            assert run.walk(lambda m, px, py: (m.b(LEADER_TILE_X), m.b(LEADER_TILE_Y)) == TRUCE_PILLAR) is not None
            machine.run(4, "DOWN")
            machine.run(3, "A")
            machine.run(40)
            assert run.dialog(), "the pillar's question"
            machine.run(3, "A")
            assert dialogs_until_in_hand(run, TRUCE_CANYON_600), "Truce Canyon"
            assert machine.b(A.EVENT_MEMORY) == STORY_600_AGAIN
            run.checkpoint("truce_600")
        if later <= 3:
            # Tonics, Revives and Shelters from the village's shop by the
            # canyon (the cave and the R Series spent them); then Zenan
            # Bridge's captain asks for supplies.
            assert leave_fighting(run, "7000"), "the canyon's lower part"
            assert leave_fighting(run, "f101"), "the 600 A.D. world map"
            assert run.world_go(*TRUCE_600), "the village"
            world_enter(run, TRUCE_600_SHOP)
            buy(run, SHOP_COUNTER, [(TONIC_ROW, 60, 10), (REVIVE_ROW, 3, 200), (SHELTER_ROW, 3, 150)])
            run.say("supplies", "tonics", count_of(machine, policies.TONIC), "revives", count_of(machine, policies.REVIVE),
                    "shelters", count_of(machine, SHELTER))
            assert leave_fighting(run, "f101"), "out of the shop"
            run.wait_world_control()
            world_enter(run, ZENAN_BRIDGE_600)
            walking(run)
            assert run.talk(ZENAN_CAPTAIN), "the captain"
            assert in_hand(run)
            assert machine.b(ZENAN_FLAGS) & SUPPLIES_ASKED, "supplies asked for"
            run.checkpoint("zenan_captain")
        if later <= 4:
            # North through Guardia Forest to the castle's kitchen, where the
            # chef hears of it.
            assert leave_fighting(run, "f101"), "off the bridge"
            run.wait_world_control()
            world_enter(run, GUARDIA_FOREST_600)
            through(run, 296, 24, lambda: run.map_id() == "f101" and run.world_pos()[1] < 260)
            run.wait_world_control()
            world_enter(run, GUARDIA_CASTLE_600)
            walking(run)
            through(run, *KITCHEN_DOOR, lambda: run.map_id() == GUARDIA_KITCHEN, rounds=10)
            walking(run)
            assert run.talk(CHEF, done=lambda: machine.b(ZENAN_FLAGS) & CHEF_TOLD), "the chef"
            assert in_hand(run)
            run.checkpoint("chef")
        if later <= 5:
            # Back in the hall, the soldier by the door hands over the
            # kitchen's jerky.
            assert leave_fighting(run, CASTLE_HALL), "the castle's hall"
            walking(run)
            assert run.walk(lambda m, px, py: m.b(ZENAN_FLAGS) & JERKY_GIVEN or not m.b(A.FIELD_PLAYER_CONTROL)
                            or m.b(A.DIALOG_OPEN)) is not None, "the soldier with the jerky"
            assert in_hand(run, 20000)
            assert count_of(machine, JERKY), "the jerky"
            run.checkpoint("jerky")
        if later <= 6:
            # South through the forest again, and to the bridge.
            assert leave_fighting(run, "f101"), "out of the castle"
            run.wait_world_control()
            world_enter(run, GUARDIA_FOREST_600, row=15)
            through(run, 296, 760, lambda: run.map_id() == "f101" and run.world_pos()[1] > 260)
            run.wait_world_control()
            world_enter(run, ZENAN_BRIDGE_600)
            walking(run)
            run.checkpoint("zenan_bridge")
        if later <= len(ZENAN) - 1:
            walking(run)
            zombor.play(run, max(3, later - 7 + 3), args.seed, before_zombor=drink_tonics)
        later -= len(ZENAN)
        if later <= 0:
            # Off the bridge, south to Dorino's shop: Tonics for the mountains'
            # fights and Mid Tonics for Masa and Mune.
            assert leave_fighting(run, "f101"), "off the bridge"
            run.wait_world_control()
            world_enter(run, DORINO_SHOP)
            buy(run, DORINO_COUNTER, [(DORINO_TONIC_ROW, 60, 10), (DORINO_MID_TONIC_ROW, 40, 100)])
            run.say("supplies", "tonics", count_of(machine, policies.TONIC),
                    "mid tonics", count_of(machine, policies.MID_TONIC), "gold", gold(machine))
            assert leave_fighting(run, "f101"), "out of the shop"
            run.checkpoint("dorino_supplies")
        if later <= 1:
            run.wait_world_control()
            toward(run, *DENADORO_FOOT, lambda: run.world_pos() == DENADORO_FOOT, step=8, limit=2000, world=True)
            assert machine.b(A.EVENT_MEMORY) == STORY_ZOMBOR, "the story as Zombor left it"
            run.checkpoint("denadoro_foot")
        if later <= 2:
            # Into the mountains and up the entrance's west way; on the way
            # Ozzie's monsters set on the party (story $5D).
            world_enter(run, DENADORO_MTS)
            walking(run)
            wx, wy = DENADORO_WEST_WAY
            for _ in range(10):
                assert in_hand(run, policy=MOUNTAIN_FIGHTS), "the field in hand"
                if abs(run.me()[0] - wx) <= 6 and run.me()[1] < wy:
                    break
                run.walk_here(lambda m, px, py: (abs(px - wx) <= 6 and py < wy) or not m.b(A.FIELD_PLAYER_CONTROL)
                              or m.b(A.DIALOG_OPEN))
                machine.run(30)
            for _ in range(10):
                if run.map_id() != DENADORO_ENTRANCE:
                    break
                machine.run(8, "UP")
            assert run.map_id() == DENADORO_WAY_UP, "the way up"
            assert machine.b(A.EVENT_MEMORY) == STORY_DENADORO, "Ozzie's monsters"
            run.checkpoint("denadoro")
        if later <= 3:
            for want, aim in DENADORO_CLIMB[:-2]:
                climb(run, want, aim)
            run.checkpoint("denadoro_falls")
        if later <= 4:
            # Down to the save point below the top. The party trains on the
            # monsters about it until all are at MASA_AND_MUNE_LEVEL, and a
            # Shelter there restores everyone before Masa and Mune.
            for want, aim in DENADORO_CLIMB[-2:]:
                climb(run, want, aim)
            train(run, MASA_AND_MUNE_LEVEL, DENADORO_AWAY, DENADORO_BACK)
            walking(run)
            assert run.walk_here(lambda m, x, y: m.b(SAVE_POINT_FLAGS) & ON_A_SAVE_POINT) is not None, "the save point"
            machine.run(30)
            rest_with_a_shelter(run)
            run.checkpoint("denadoro_trained")
        if later <= 5:
            # Up to the top and into the cave, where Masa and Mune stop the
            # party and fight, first apart and then as one; beaten, they give
            # up the blade.
            for want, aim in DENADORO_TOP:
                climb(run, want, aim)
            walking(run)
            assert run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN)) is not None
            assert run.dialogs_until(lambda: early.battle_ready(run), 20000), "Masa and Mune"
            assert in_hand(run, 30000, args.seed, masa_and_mune), "Masa and Mune beaten"
            # Up the cave's stair to where the blade lies, which they give up;
            # the scene ends with the party leaping down the mountainside on
            # the world map, to land below the entrance.
            walking(run)
            assert run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN)) is not None
            assert run.dialogs_until(lambda: run.map_id() == WORLD_600, 20000), "out of the mountains"
            machine.run(120)                # the leap starts a moment after the map shows
            landed = None
            while landed != run.world_pos():
                landed = run.world_pos()
                machine.run(30)
            assert run.wait_world_control(), "the world map in hand"
            assert machine.b(A.EVENT_MEMORY) == STORY_BLADE and count_of(machine, BENT_SWORD), "the blade"
            run.checkpoint("masa_mune")
        later -= len(DENADORO)
        if later <= 0:
            # Tata, at home in Porre, owns up and gives the Hero Medal; the
            # party drinks Tonics there after Masa and Mune.
            LEG_B.world_toward(run, *PORRE_NEAR, near=32)
            world_enter(run, TATAS_HOUSE)
            walking(run)
            if machine.b(A.EVENT_MEMORY) < STORY_HERO_MEDAL:
                run.talk(TATA, done=lambda: run.dialog() or machine.b(A.EVENT_MEMORY) >= STORY_HERO_MEDAL)
            assert in_hand(run)
            assert machine.b(A.EVENT_MEMORY) == STORY_HERO_MEDAL and count_of(machine, HERO_MEDAL), "the Hero Medal"
            drink_tonics(run)
            run.checkpoint("hero_medal")
        if later <= 1:
            # Frog greets the party in his burrow; shown the medal, he steps
            # away from his basket, which holds the Masamune's hilt
            # (thonky.com's walkthrough, "Tata and the Frog").
            assert leave_fighting(run, WORLD_600), "out of Tata's house"
            run.wait_world_control()
            world_enter(run, CURSED_WOODS)
            assert leave_fighting(run, FROGS_BURROW), "Frog's burrow"
            walking(run)
            run.walk_here(lambda m, x, y: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN))
            assert in_hand(run), "Frog's greeting"
            walking(run)
            fx, fy = run.object_pos(FROG_IN_THE_BURROW)
            assert run.go(fx, fy, tolerance=6), "beside Frog"   # against him, below his basket
            machine.run(3, "UP")
            machine.run(5)
            for _ in range(3):
                machine.run(3, "A")
                machine.run(30)
                if run.dialog():
                    break
            assert run.dialog(), "Frog"
            read_through(run)
            assert in_hand(run)
            walking(run)
            run.talk(FROGS_BASKET, done=lambda: run.dialog() or machine.b(A.EVENT_MEMORY) >= STORY_HILT)
            assert settle_the_scene(run, STORY_HILT) and count_of(machine, BENT_HILT), "the hilt"
            run.checkpoint("bent_hilt")
        if later <= 2:
            # Out of the woods, to Truce Canyon's Gate, which goes to the End
            # of Time; its Medina pillar to 1000 A.D., and Melchior's hut,
            # where he names the Dreamstone the sword wants.
            LEG_B.woods_out(run)
            LEG_B.world_toward(run, *TRUCE_CANYON_NEAR, near=40)
            world_enter(run, TRUCE_CANYON)
            assert leave_fighting(run, TRUCE_CANYON_600), "the Gate's clearing"
            walking(run)
            gx, gy = CANYON_GATE_SPOT
            assert run.walk_here(lambda m, x, y: abs(x - gx) <= 2 and abs(y - gy) <= 2) is not None, "beside the Gate"
            machine.run(3, "RIGHT")
            machine.run(3, "A")
            assert dialogs_until_in_hand(run, END_OF_TIME_HALL), "the End of Time"
            LEG_B.medina(run)
            LEG_B.melchior(run)
            assert machine.b(A.EVENT_MEMORY) == STORY_MELCHIOR, "Melchior"
            run.checkpoint("melchior_told")
        if later <= 3:
            # Back to the End of Time by the Medina house's Gate, and its
            # south-west pillar to the Mystic Mountains' top; down them to the
            # 65,000,000 B.C. world map.
            LEG_B.medina_gate(run)
            LEG_B.pillar(run, PILLAR_65M, MYSTIC_MOUNTAIN_TOP)
            for want in MOUNTAIN_WAY_DOWN:
                assert leave_fighting(run, want), want
            assert machine.b(A.EVENT_MEMORY) == STORY_PREHISTORY, "65,000,000 B.C."
            run.checkpoint("prehistory")
        if later <= 4:
            # In the chief's hut at Ioka, Ayla takes the party to the feast.
            run.wait_world_control()
            world_enter(run, CHIEFS_HUT)
            walking(run)
            assert speak(run, AYLA_IN_THE_HUT), "Ayla"
            assert dialogs_until_in_hand(run, FEAST), "the feast"
            run.checkpoint("ioka_feast")
        if later <= 5:
            # The feast's talk, in the order that brings on Ayla's soup race
            # for her red rock, the Dreamstone: Marle, Lucca's neighbour,
            # Lucca and Ayla; Ayla again; Marle and Lucca; then Ayla, whose
            # challenge is accepted (the first row). In the race A is pressed
            # as fast as it counts (its "give up?" has "No way!" on the second
            # row, so Down goes with it). The party wakes in the hut to find
            # the Gate Key gone.
            for talks in FEAST_TALKS:
                walking(run)
                for obj in talks:
                    speak(run, obj)
                    read_through(run)
                assert in_hand(run), "the feast in hand"
            walking(run)
            assert speak(run, FEAST_AYLA), "Ayla's challenge"
            for _ in range(SOUP_RACE_WINDOWS):
                machine.run(40)
                if run.dialog():
                    machine.run(3, "A")
            for _ in range(3000):
                if machine.b(A.EVENT_MEMORY) >= STORY_SOUP:
                    break
                machine.run(1, "A")
                machine.run(1)
                machine.run(1, "DOWN")
                machine.run(1)
            assert machine.b(A.EVENT_MEMORY) >= STORY_SOUP, "the race won"
            # The night: windows confirmed until the party is in the hut in
            # the morning (where Ayla's snores open windows of their own).
            assert run.dialogs_until(lambda: machine.b(A.EVENT_MEMORY) >= STORY_DREAMSTONE
                                     and run.map_id() == CHIEFS_HUT_MAP, 20000), "the night"
            assert count_of(machine, DREAMSTONE_ITEM) and not count_of(machine, GATE_KEY), "the Dreamstone; the Gate Key gone"
            run.checkpoint("soup_race")
        if later <= 6:
            # Ayla snores, a window at a time, which holds the party still
            # while it is open. Walked up to her row and across the hut to her,
            # the windows confirmed, the party speaks to her, she wakes and
            # joins it to go after the Gate Key, with Marle, and the
            # party goes out to the world map.
            wx, wy = AYLA_ASLEEP_REACH
            run.player = machine.w(A.FIELD_PARTY_OBJECTS) // 2   # the windows keep find_player from it
            for way, reached in (("UP", lambda: run.me()[1] <= wy), ("RIGHT", lambda: run.me()[0] >= wx)):
                for _ in range(600):
                    if reached():
                        break
                    machine.run(1, way)
            for _ in range(400):
                if machine.b(A.EVENT_MEMORY) >= STORY_AYLA:
                    break
                if run.dialog():
                    machine.run(3, "A")
                machine.run(30)
            # The menus ask who Ayla replaces: cancelled, the party asks
            # whether it stays as it is (Crono, Ayla, Marle), confirmed.
            assert run.dialogs_until(lambda: machine.b(MENU_SCREEN) == EXCHANGE_SCREEN, 6000), "who Ayla replaces"
            machine.run(60)
            machine.run(3, "B")
            assert run.dialogs_until(lambda: bool(machine.b(A.FIELD_PLAYER_CONTROL)), 3000), "the party as it is"
            assert in_hand(run, 30000), "Ayla's choice"
            assert machine.b(A.EVENT_MEMORY) == STORY_AYLA and AYLA in machine.peek(A.PARTY_MEMBERS, 3), "Ayla joins"
            assert leave_fighting(run, WORLD_65M), "out of the hut"
            run.wait_world_control()
            run.checkpoint("ayla_joins")
        later -= len(DREAMSTONE)
        if later <= len(FROM_LEG_B) - 1:
            run.chapter = LEG_B_CHAPTER
            LEG_B.play(run, LEG_B_FROM + max(0, later), args.seed)
            run.chapter = ""
        run.finish("the playthrough's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
