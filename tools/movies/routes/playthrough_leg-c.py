"""Leg C of the playthrough: from Tyrano Lair to the Ocean Palace's Lavos.

Save: ctsave08 slot 1 (fantasyanime archive; Crono, Ayla and Marle at Tyrano
Lair's save point, $12F, Azala and the Black Tyrano ahead).

The leg plays the Black Tyrano and Lavos's fall, 12000 B.C. and Zeal, the
Epoch, Terra Cave and the Mountain of Woe, the Ocean Palace and the Golem
Twins, and ends as Lavos rises from the Mammon Machine. Where another route
already plays a stretch from a save, the leg calls its `play(run, start)` from
the state it reached. The party and items are the save's.
"""
from .. import addresses as A
from .. import bfs
from .. import policies
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import world_enter
from ._epoch import DIAL_PERIOD, entrance, land_by
from . import ozzies_fort
from .playthrough import naming
from . import playthrough
from . import _trial_party as early
from ._zeal_navigation import approach
from . import black_tyrano
from . import golem_twins
from . import ocean_palace_lavos
from . import menu_sweep as menus
from . import giga_gaia
from . import mud_imp
from . import zeal_palace

SAVE = "saves/fantasyanime/ctsave08-zeal.srm"
SLOT = 0
STORY = 0x7F0000
CRONO, MARLE, AYLA = 0, 1, 5   # character ids
LAIR_RUINS = 0x133          # the crater Lavos left, 65,000,000 B.C.
WORLD_65M, LAIR_RUINS_MAP = "f301", "3301"
LAIR_RUINS_GATE = 8         # an object of the Lair Ruins
CAVE_12KBC, SNOWFIELD = "5f01", "f401"
ZEAL_HALL, MAMMON_PASSAGE, SEALED_DOOR_HALL, QUEENS_ROOM, QUEENS_HALL = "9101", "4a01", "4901", "4c01", "4e01"
SEALED_DOOR = (328, 144)    # in Zeal Palace's hall, up to the Queen's door
EXILED = 0xAB               # the story once Schala has sent the party back
TRAVEL_ON_FOOT, TRAVEL_ON_A_DACTYL = 0, 3
MYSTIC_MTS = 0x112
MYSTIC_MTS_FOOT, MYSTIC_MTS_TOP, MYSTIC_MTS_GATE = "1201", "1101", "1001"
MYSTIC_LEDGE = (170, 104)   # above the Gate, where the party jumps down to it
END_OF_TIME = "d001"
GATE_TILE = 0x7F0212        # the leader's tile at the End of Time, column then row two bytes on
PILLAR_2300 = (13, 10)      # the End of Time's pillar of light to Proto Dome
PROTO_DOME_GATE, PROTO_DOME, WORLD_2300 = "e300", "e200", "f201"
LAB32_EAST = 0x0E1
LAB32_EAST_MAP, LAB32, LAB32_WEST = "e100", "e000", "df00"
SEWER_ACCESS = 0x0EC
SEWER, SEWER_DEEP = "ec00", "ed00"
SEWER_DEEP_STAIR = (456, 344)   # $0ED's way up into the sewer's lower half
SEWER_SWITCH = (344, 410)       # below the switch, in the pocket under the shutter
KEEPERS_DOME = 0x0F1
BELTHASARS_HALL, HANGAR = "f200", "f300"
KEEPERS_SEALED_DOOR = (640, 352)
HALL_SEALED_DOOR = (48, 88)
HALL_RECORDS = range(8, 14)     # objects of Belthasar's hall
THE_EPOCH = 13              # an object of the hangar
EPOCH_NOSE = 300            # the hangar's row below the Epoch's nose
PERIOD_12KBC = 5            # the Epoch's dial (_epoch's periods)
TERRA_CAVE = 0x182
ALGETTY = "7c01"
ALGETTY_SAVE_SPOT = (680, 700)  # below Algetty's save point
MELCHIOR = 10                   # an object of Terra Cave's inner room
TERRA_CAVE_MAP = "8201"
# The Skyways from Terra Cave's snowfield to Zeal Palace: a world-map door
# into each Skyway, how long to hold up on its beam, and the maps on the way.
SKYWAYS = [((864, 688), "5101", 90, "6101", "f501"), ((936, 600), "5001", 180, "6201", "f401"),
           ((992, 192), "6401", 130, "6001", "f501"), ((728, 496), "8501", 140, "8501", "f501"),
           ((816, 456), "8501", 140, "8501", "f501")]
ZEAL_PALACE_DOOR = (808, 400)
OCEAN_GATE = 8                  # the Gate Dalton opened in the Queen's hall
PALACE_ENTRY, PALACE_HALL, PALACE_EAST, PALACE_SOUTH_EAST = "9401", "9501", "9601", "9701"
PALACE_DOWN, PALACE_STAIRS, PALACE_LANDING = "9801", "9901", "9a01"
HALL_EAST_DOOR, HALL_SOUTH_EAST_DOOR, HALL_DOWN = (47, 24, "RIGHT"), (35, 42, "DOWN"), (23, 47, "DOWN")
EAST_WING_DOOR = (546, 161)     # its first room's way on to the button's
PALACE_BUTTONS = 0x7F0162       # bit 2 the east wing's, bit 0 the south-east bridge's
PLATFORMS_TOP = 1700            # the stairs' lowest platform, before the clogged stairs
CLOGGED_STAIRS = (376, 1790)    # where Masa and Mune's scene begins below it
PALACE_SAVE_SPOT = (56, 190)    # below the landing's save point
PALACE_LANDED = 0xC6            # the story once Masa and Mune have spoken
ELEVATOR, ELEVATOR_SHAFT, ELEVATOR_FOOT, PALACE_FLOOR_MAP, TWINS_SAVE_ROOM = "9b01", "a001", "a201", "9c01", "9d01"
ELEVATOR_ROW = 20               # on the elevator, below its row 18 where the first fight waits
ELEVATOR_DOWN = (3 * 16 + 8, 20 * 16 + 8)   # beside its left orb, which sends it down
FLOOR_SWITCHES = [((28, 12), (623, 488), 0x20), ((4, 12), (256, 488), 0x10)]   # door, below the switch, bit
TWINS_SAVE_SPOT = (216, 126)    # below the save point before the Twins
BEAST_NEST, WOE_FOOT, WOE_MIDDLE, WOE_SAVE_POINT = "8401", "8a01", "8801", "8901"
WOE_FOOT_EXIT = (576, 544)      # $18A's way on to $188
WOE_SAVE_SPOT = (440, 128)      # below $189's save point
ON_A_SAVE_POINT = (0x7F01CF, 0x80)
TYRANO = black_tyrano.PHASES[1:]
# Out of the scene onto the 65,000,000 B.C. map, into the Lair Ruins and
# through the Gate there to 12000 B.C., out onto the snowfield's map.
GATE = ["lair_ruins", "gate_12kbc", "snowfield"]
ZEAL = zeal_palace.PHASES[1:]
# Back through the sealed door to the Queen, Dalton's Golem, and Schala's
# spell that sends the party back to the Lair Ruins with the Gate sealed.
EXILE = ["queen_door", "golem", "golem_won", "exiled"]
# A dactyl off the plateau to the Mystic Mountains, whose Gate goes to the
# End of Time; its 2300 A.D. pillar, Proto Dome and Lab 32 to the Sewer
# Access; through it to Keeper's Dome, Belthasar's Epoch, and the Epoch's
# first flight to 12000 B.C.: Algetty, under Terra Cave.
EPOCH = ["dactyl", "mystic_mts", "mystic_gate", "end_of_time", "proto_dome", "lab32", "sewer", "sewer_lower",
         "sewer_switch", "sewer_out", "keepers_dome", "hangar", "belthasar", "epoch_dial", "epoch_12kbc",
         "algetty", "algetty_rested"]
MUD_IMP = mud_imp.PHASES[1:]
# Up the chain from the nest onto Mt. Woe and across its islands to the
# last save point, where a Shelter restores the party the climb wore down.
WOE = ["woe_chain", "woe_ambush", "woe_middle", "woe_save_point", "woe_rested"]
GIGA_GAIA = giga_gaia.PHASES[1:]
# Melchior's word in Terra Cave, a rest in Algetty, the Skyways back to Zeal
# Palace and Dalton in the Queen's room.
DALTON = ["melchior", "algetty_again", "terra_cave_out", "skyways", "queens_hall", "dalton", "dalton_won"]
# Through the Queen's gate after him into the Ocean Palace: the two buttons
# that open its way down, the stairs and platforms to Masa and Mune, and a
# rest at the save point there.
OCEAN_PALACE = ["ocean_palace", "first_button", "second_button", "palace_stairs", "platforms", "masa_and_mune",
                "palace_rested"]
# The elevator down through its fights to the palace's floor, a switch in
# each of the floor's side rooms for the bridge, and a rest before Dalton's
# Golem Twins; then Lavos rising from the Mammon Machine, where the leg ends.
PALACE_FLOOR = ["elevator", "palace_floor", "east_switch", "west_switch", "twins_rested"]
TWINS = golem_twins.PHASES[1:4]
LAVOS = ocean_palace_lavos.PHASES[2:4]
PHASES = (["loaded"] + TYRANO + GATE + ZEAL + EXILE + EPOCH + MUD_IMP + WOE + GIGA_GAIA + DALTON + OCEAN_PALACE
          + PALACE_FLOOR + TWINS + LAVOS)
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# Leg C of the playthrough (route playthrough_leg-c): from Tyrano Lair to the Ocean Palace's Lavos.
# Save: ctsave08 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def load(run):
    """Power on and load the save's slot, until the party is in hand."""
    machine = run.machine
    machine.run(1620)
    machine.run(5, "START")
    machine.run(2420 - machine.frame)
    assert run.select_slot(SLOT)
    machine.run(4, "A")
    for _ in range(200):
        machine.run(10)
        if run.map_id() not in ("0000", "b101"):
            break
    assert run.wait_control(), "the party in hand"
    assert run.map_id() == black_tyrano.SAVE_ROOM, "Tyrano Lair's save point"


def world_in_hand(run, world, max_frames=20000):
    """Close windows until the party walks world map `world`. The scene after
    the Black Tyrano shows the map, leaves it for Ayla's people and comes back."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        if run.dialog():
            machine.run(3, "A")
            machine.run(40)
        elif run.map_id() == world and run.wait_world_control(30):
            return True
        else:
            machine.run(10)
    return False


def party_policy(bot, state, rng, inventory):
    """policies.party's reviving and healing with this save's bag, which by
    Zeal has no Tonics left: a Lapis when the party is worn down, Marle's
    Cure 2 and the Revives, a Mid Ether for a healer out of MP. Otherwise
    weapons: the Ocean Palace's Scouts drink the element they are hit with."""
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    if all(state.hp(slot) for slot in members):
        low = sorted((slot for slot in members if policies.fraction(state, slot) < 0.5),
                     key=lambda slot: policies.fraction(state, slot))
        if len(low) >= 2 or low and policies.fraction(state, low[0]) < 0.3:
            action = policies.use(inventory, policies.LAPIS, None)
            if action and rng.random() < 0.9:
                return action
    action = policies.party(bot, state, rng, inventory)
    if action[0] == "item" or action[0] == "tech" and action[2] is not None and action[2] < 3:
        return action
    living = policies.enemies(state)
    if state.mp(state.member) < 10 and policies.party_ids(bot)[state.member] == MARLE:
        ether = policies.use(inventory, policies.MID_ETHER, state.member)
        if ether and rng.random() < 0.5:
            return ether
    return ("attack", min(living, key=lambda slot: state.hp(slot)) if living else None)


def dalton_policy(bot, state, rng, inventory):
    """Dalton halves a member's HP with each blow, and leaving he belches
    fire over the whole party: a Lapis whenever anyone is down to half,
    techs otherwise, to have him gone before the party is worn thin."""
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    if all(state.hp(slot) for slot in members) and any(policies.fraction(state, slot) < 0.6 for slot in members):
        action = policies.use(inventory, policies.LAPIS, None)
        if action:
            return action
    return policies.party(bot, state, rng, inventory)


def twins_policy(bot, state, rng, inventory):
    """The Golem Twins with Crono, Ayla and Marle: alternate Lightning 2 and
    Ice 2 over both so neither element is theirs to copy for long, Ayla's
    fists, Marle's Life 2 and Cure 2 between, and the Ethers for the MP."""
    member = state.member
    character = policies.party_ids(bot)[member]
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    dead = [slot for slot in members if not state.hp(slot)]
    if dead:
        if character == MARLE and state.mp(member) >= 15:
            return ("tech", 7, dead[0])
        action = policies.use(inventory, policies.REVIVE, dead[0])
        if action:
            return action
    low = [slot for slot in members if state.hp(slot) and policies.fraction(state, slot) < 0.45]
    if len(low) >= 2:
        action = policies.use(inventory, policies.LAPIS, None)
        if action:
            return action
    if low and character == MARLE and state.mp(member) >= 5:
        return ("tech", 6, min(low, key=lambda slot: policies.fraction(state, slot)))
    if character != AYLA and state.mp(member) < 20:
        action = policies.use(inventory, policies.FULL_ETHER, member) or policies.use(inventory, policies.MID_ETHER, member)
        if action:
            return action
    living = policies.enemies(state)
    if character == AYLA or not living:
        return ("attack", min(living, key=lambda slot: state.hp(slot)) if living else None)
    return ("tech", 4 if character == CRONO else 5, None)


def in_hand(run, max_frames=12000, seed=1):
    return playthrough.in_hand(run, max_frames, seed, party_policy)


def win(run, seed=1):
    playthrough.win(run, seed, party_policy)


def leave_fighting(run, want, max_rounds=12, seed=1):
    return playthrough.leave_fighting(run, want, max_rounds, seed, policy=party_policy)


def through(run, x, y, arrived, seed=1, rounds=10):
    playthrough.through(run, x, y, arrived, seed, rounds, policy=party_policy)


def stuck_in_hand(run, max_frames=30000, seed=1):
    """in_hand, for a scene that also waits on a press with no window open:
    Belthasar's, before he has the party name the Epoch."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        if in_hand(run, 600, seed):
            return True
        if not run.dialog() and not naming(run):
            machine.run(3, "A")
            machine.run(37)
    return False


def onward(run, avoid=frozenset(), rounds=20, seed=1):
    """Walk on to any map but this one and those in `avoid`, winning the
    fights on the way: leave_fighting for a way out not known by name."""
    machine = run.machine
    here = run.map_id()
    away = lambda: run.map_id() not in avoid | {here}
    for _ in range(rounds):
        assert in_hand(run), "the field in hand"
        if away():
            return True
        moves = run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() not in avoid | {here}
                         or not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN))
        run.say("onward", "steps", moves and len(moves), "now", run.map_id(), "at", run.me())
        if away():
            return True
        if moves is None:
            return False
        machine.run(30)
        assert in_hand(run, 30000, seed), "the fight or scene on the way ends"
    return away()


def walk_fighting(run, goal, seed=1, rounds=12, max_nodes=6000):
    """Walk this map until `goal(x, y)`, winning the fights that stop the walk."""
    machine = run.machine
    for _ in range(rounds):
        assert in_hand(run), "the field in hand"
        if goal(*run.me()):
            return True
        moves = run.walk_here(lambda m, px, py: goal(px, py) or not m.b(A.FIELD_PLAYER_CONTROL)
                              or m.b(A.DIALOG_OPEN), max_nodes=max_nodes)
        if moves is None:
            return False
        if goal(*run.me()):
            return True
        machine.run(30)
        assert in_hand(run, 30000, seed), "the fight or scene on the way ends"
    return False


def head_for(run, x, y, seed=1, rounds=12):
    """approach (x, y) on this map, winning the fights that jump the party on
    the way and searching again from where each one leaves it."""
    machine = run.machine
    here = run.map_id()
    ambushed = lambda m: not m.b(A.FIELD_PLAYER_CONTROL) and m.peek(A.MAP_ID, 2).hex() == here
    for _ in range(rounds):
        assert in_hand(run, 30000, seed), "the field in hand"
        px, py = run.me()
        if abs(px - x) < 6 and abs(py - y) < 6:
            return True
        approach(run, x, y, stop=ambushed)
        machine.run(30)
    return False


def take_exit(run, column, row, direction, seed=1):
    """Through the exit on tile (column, row) of this map: to the tile before
    it, then on in `direction` until the next map is up."""
    machine = run.machine
    here = run.map_id()
    dx, dy = {"UP": (0, 1), "DOWN": (0, -1), "LEFT": (1, 0), "RIGHT": (-1, 0)}[direction]
    assert head_for(run, (column + dx) * 16 + 8, (row + dy) * 16 + 8, seed), "before the exit"
    for _ in range(20):
        machine.run(8, direction)
        if run.map_id() != here:
            break
    assert run.map_id() != here, "through the exit"
    assert in_hand(run, 30000, seed), "the next map in hand"


def exile(run, ahead, seed):
    """From the Mammon Machine's room, the pendant opens the Queen's door:
    Dalton's Golem, and Schala sends the party back to the Lair Ruins."""
    machine = run.machine
    if ahead("queen_door"):
        assert run.exit_to(MAMMON_PASSAGE) and run.exit_to(ZEAL_HALL), "Zeal Palace's hall"
        zeal_palace.doorway(run, *SEALED_DOOR, "UP", SEALED_DOOR_HALL)
        # Up the hall to the Queen's door, which the charged pendant opens.
        for _ in range(80):
            machine.run(8, "UP")
            if not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        assert run.wait_control(), "the door opened"
        for _ in range(60):
            machine.run(8, "UP")
            if run.map_id() != SEALED_DOOR_HALL:
                break
        assert run.map_id() == QUEENS_ROOM and run.wait_control(), "the Queen's room"
        run.checkpoint("queen_door")
    if ahead("golem"):
        for _ in range(80):
            machine.run(8, "UP")
            if run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        assert run.dialogs_until(lambda: A.battle_menu_open(machine), 8000), "Golem's battle"
        run.checkpoint("golem")
    if ahead("golem_won"):
        win(run, seed)
        run.checkpoint("golem_won")
    if ahead("exiled"):
        assert run.dialogs_until(lambda: run.map_id() == LAIR_RUINS_MAP and machine.b(STORY) == EXILED, 20000), \
            "back in the Lair Ruins"
        assert run.wait_control(), "the party in hand"
        run.checkpoint("exiled")


def dactyl_to(run, location):
    """Board the dactyl that waits by the Lair Ruins, fly it to `location`,
    land on the nearest open ground and walk in."""
    machine = run.machine
    moves, _ = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT,
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(), confirm=True, max_nodes=800)
    machine.run(60)
    assert moves is not None and machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL, "aboard the dactyl"
    land_by(run, location)


def to_the_epoch(run, ahead, seed):
    """From the Lair Ruins to Algetty in 12000 B.C., by the End of Time,
    2300 A.D. and Belthasar's Epoch."""
    machine = run.machine
    if ahead("dactyl"):
        assert run.exit_to(WORLD_65M), "out onto the plateau"
        assert run.wait_world_control(), "the map in hand"
        dactyl_to(run, MYSTIC_MTS)
        assert in_hand(run), "the Mystic Mountains in hand"
        run.checkpoint("dactyl")
    if ahead("mystic_mts"):
        through(run, 120, 0, lambda: run.map_id() != MYSTIC_MTS_FOOT, seed=seed, rounds=20)
        assert run.map_id() == MYSTIC_MTS_TOP and in_hand(run), "up the mountain"
        run.checkpoint("mystic_mts")
    if ahead("mystic_gate"):
        assert run.exit_other(frozenset({MYSTIC_MTS_FOOT, WORLD_65M})), "the Gate's slope"
        assert run.map_id() == MYSTIC_MTS_GATE and in_hand(run), "the slope in hand"
        run.checkpoint("mystic_gate")
    if ahead("end_of_time"):
        # Off the ledge above the Gate ("jump down?" answered yes), and in.
        assert run.go(*MYSTIC_LEDGE), "the ledge"
        machine.run(60, "DOWN")
        machine.run(30)
        machine.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME, 3000), "the End of Time"
        assert run.wait_control(), "the party at the End of Time"
        run.checkpoint("end_of_time")
    if ahead("proto_dome"):
        assert run.walk(lambda m, x, y: (m.b(GATE_TILE), m.b(GATE_TILE + 2)) == PILLAR_2300) is not None, \
            "2300 A.D.'s pillar"
        # The pillar takes the party once it has settled on it: a step that
        # stops on the pillar's tile can leave it a press short.
        for nudge in ("UP", "DOWN", "UP", "DOWN"):
            machine.run(4, nudge)
            machine.run(3, "A")
            machine.run(60)
            machine.run(3, "A")
            if run.dialogs_until(lambda: run.map_id() == PROTO_DOME_GATE, 900):
                break
        assert run.map_id() == PROTO_DOME_GATE, "Proto Dome's Gate"
        assert run.wait_control()
        assert run.exit_to(PROTO_DOME), "Proto Dome"
        assert leave_fighting(run, WORLD_2300), "the 2300 A.D. map"
        run.checkpoint("proto_dome")
    if ahead("lab32"):
        run.wait_world_control()
        world_enter(run, LAB32_EAST)
        assert in_hand(run)
        assert onward(run, frozenset({WORLD_2300}), seed=seed) and run.map_id() == LAB32, "Lab 32"
        assert onward(run, frozenset({LAB32_EAST_MAP}), seed=seed) and run.map_id() == LAB32_WEST, "its west end"
        assert leave_fighting(run, WORLD_2300), "out by Arris Dome"
        run.checkpoint("lab32")
    if ahead("sewer"):
        run.wait_world_control()
        world_enter(run, SEWER_ACCESS)
        assert onward(run, frozenset({WORLD_2300}), seed=seed) and run.map_id() == SEWER_DEEP, "the fishmen's floor"
        run.checkpoint("sewer")
    if ahead("sewer_lower"):
        # Leave everything on this floor alone; its stair up comes out in
        # the sewer's lower half.
        x, y = SEWER_DEEP_STAIR
        assert walk_fighting(run, lambda px, py: abs(px - x) < 10 and abs(py - y) < 10, seed), "the stair up"
        machine.run(40, "UP")
        machine.run(60)
        assert run.map_id() == SEWER and in_hand(run), "the sewer's lower half"
        run.checkpoint("sewer_lower")
    if ahead("sewer_switch"):
        # The notice on the machine says to go round the back and press the
        # switch: it is in the pocket below the shutter, faced from below.
        x, y = SEWER_SWITCH
        assert walk_fighting(run, lambda px, py: abs(px - x) < 6 and abs(py - y) < 6, seed), "below the switch"
        machine.run(40, "UP")
        machine.run(3, "A")
        machine.run(60)
        assert in_hand(run)
        run.checkpoint("sewer_switch")
    if ahead("sewer_out"):
        assert onward(run, frozenset({SEWER_DEEP}), seed=seed) and run.map_id() == WORLD_2300, "out by Keeper's Dome"
        run.checkpoint("sewer_out")
    if ahead("keepers_dome"):
        run.wait_world_control()
        world_enter(run, KEEPERS_DOME)
        assert in_hand(run)
        # Past the sleeping Nu, the pendant opens the dome's sealed door.
        assert run.go(*KEEPERS_SEALED_DOOR), "the sealed door"
        machine.run(20, "UP")
        machine.run(3, "A")
        assert leave_fighting(run, BELTHASARS_HALL), "Belthasar's hall"
        run.checkpoint("keepers_dome")
    if ahead("hangar"):
        # Belthasar's records glimmer along the hall; the way to its far
        # sealed door is open once the last has been read.
        for record in HALL_RECORDS:
            assert run.talk(record), "a record of Belthasar's"
        assert run.go(*HALL_SEALED_DOOR), "the hall's sealed door"
        machine.run(20, "UP")
        machine.run(3, "A")
        assert leave_fighting(run, HANGAR), "the hangar"
        run.checkpoint("hangar")
    if ahead("belthasar"):
        # Up the corridor, its door opened with confirm, to the Epoch's
        # nose; beside it the party wonders at it; back down, the Nu comes
        # in as Belthasar and has them name it.
        for _ in range(40):
            y = run.me()[1]
            machine.run(20, "UP")
            if run.me()[1] < EPOCH_NOSE:
                break
            if run.me()[1] == y:
                machine.run(3, "A")
                machine.run(40)
                assert in_hand(run), "the corridor in hand"
        assert run.me()[1] < EPOCH_NOSE, "up to the Epoch"
        assert run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN),
                        max_nodes=6000) is not None, "beside the Epoch"
        assert in_hand(run)
        assert run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN),
                        max_nodes=6000) is not None, "back down the hangar"
        assert stuck_in_hand(run), "the Epoch named"
        run.checkpoint("belthasar")
    if ahead("epoch_dial"):
        # Confirm beside it asks whether to board; yes opens its dial.
        assert run.talk(THE_EPOCH, done=lambda: run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL)), "the Epoch"
        assert run.dialogs_until(lambda: run.map_id() == WORLD_2300, 6000), "aboard the Epoch"
        machine.run(600)
        # Its first flight opens the dial without the Y a later one takes.
        for _ in range(8):
            if machine.b(DIAL_PERIOD) == PERIOD_12KBC:
                break
            machine.run(4, "DOWN" if machine.b(DIAL_PERIOD) < PERIOD_12KBC else "UP")
            machine.run(56)
        assert machine.b(DIAL_PERIOD) == PERIOD_12KBC, "12000 B.C. on the dial"
        run.checkpoint("epoch_dial")
    if ahead("epoch_12kbc"):
        machine.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == SNOWFIELD
                                 and machine.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT, 8000), "12000 B.C."
        run.checkpoint("epoch_12kbc")
    if ahead("algetty"):
        # Without its wings the Epoch keeps to open ground, which the
        # straight line to Terra Cave crosses; across, then down, does not.
        ozzies_fort.fly_to(run, *entrance(run, TERRA_CAVE))
        land_by(run, TERRA_CAVE)
        assert leave_fighting(run, ALGETTY), "Algetty"
        run.checkpoint("algetty")
    if ahead("algetty_rested"):
        assert run.wait_control()
        approach(run, *ALGETTY_SAVE_SPOT)
        rest(run)
        run.checkpoint("algetty_rested")


def rest(run):
    """Just below a save point: step onto it, and a Shelter from the items
    screen restores the party."""
    machine = run.machine
    flag, bit = ON_A_SAVE_POINT
    for _ in range(40):                 # the last step up onto it
        if machine.b(flag) & bit:
            break
        machine.run(1, "UP")
    assert machine.b(flag) & bit, "standing on the save point"
    count = menus.count_of(machine, menus.SHELTER)
    assert count, "a Shelter for the party"
    menus.open_menu(machine)
    menus.open_option(machine, menus.ITEMS)
    menus.use_item(run, menus.SHELTER, choose_member=True)
    machine.run(180)
    assert menus.count_of(machine, menus.SHELTER) == count - 1, "the Shelter used"
    menus.back_to_main_menu(machine)
    menus.press(machine, "B", 60)
    assert run.wait_control(), "the field after resting"


def mt_woe(run, ahead, seed):
    """Up the chain from the Beasts' nest onto Mt. Woe, and over its islands
    to the save point before the summit."""
    machine = run.machine
    if ahead("woe_chain"):
        # The vine at the nest's top, then the chain up from it.
        assert in_hand(run)
        for _ in range(10):
            machine.run(15, "UP+RIGHT")
        for _ in range(20):
            machine.run(15, "UP")
            if run.map_id() != BEAST_NEST:
                break
        assert run.dialogs_until(lambda: run.map_id() == WOE_FOOT, 3000) and in_hand(run), "Mt. Woe"
        run.checkpoint("woe_chain")
    if ahead("woe_ambush"):
        # The islands' monsters jump the party long before a walk arrives:
        # head for the way on, fight, and search again from where it ends.
        ambushed = lambda m: not m.b(A.FIELD_PLAYER_CONTROL) and m.peek(A.MAP_ID, 2).hex() == WOE_FOOT
        approach(run, *WOE_FOOT_EXIT, stop=ambushed)
        assert in_hand(run, 30000, seed), "the ambush fought"
        run.checkpoint("woe_ambush")
    if ahead("woe_middle"):
        assert leave_fighting(run, WOE_MIDDLE, seed=seed), "Mt. Woe's middle"
        run.checkpoint("woe_middle")
    if ahead("woe_save_point"):
        assert leave_fighting(run, WOE_SAVE_POINT, max_rounds=20, seed=seed), "the last save point"
        assert in_hand(run)
        run.checkpoint("woe_save_point")
    if ahead("woe_rested"):
        x, y = WOE_SAVE_SPOT
        assert walk_fighting(run, lambda px, py: abs(px - x) < 4 and abs(py - y) < 4, seed), "the save point"
        rest(run)
        run.checkpoint("woe_rested")


def to_dalton(run, ahead, seed):
    """From Melchior in Terra Cave back to Zeal Palace, where Dalton waits in
    the Queen's room."""
    machine = run.machine
    if ahead("melchior"):
        assert run.talk(MELCHIOR), "Melchior"
        run.checkpoint("melchior")
    if ahead("algetty_again"):
        assert leave_fighting(run, ALGETTY, seed=seed), "Algetty"
        assert run.wait_control()
        approach(run, *ALGETTY_SAVE_SPOT)
        rest(run)
        run.checkpoint("algetty_again")
    if ahead("terra_cave_out"):
        assert leave_fighting(run, TERRA_CAVE_MAP, seed=seed), "Terra Cave"
        assert leave_fighting(run, SNOWFIELD, seed=seed), "the snowfield"
        assert run.wait_world_control(), "the snowfield in hand"
        run.checkpoint("terra_cave_out")
    if ahead("skyways"):
        for door, skyway, hold, landing, world in SKYWAYS:
            zeal_palace.enter(run, *door, skyway)
            zeal_palace.beam(run, hold, landing)
            zeal_palace.outside(run, world)
        zeal_palace.enter(run, *ZEAL_PALACE_DOOR, ZEAL_HALL)
        run.checkpoint("skyways")
    if ahead("queens_hall"):
        zeal_palace.doorway(run, *SEALED_DOOR, "UP", SEALED_DOOR_HALL)
        assert leave_fighting(run, QUEENS_HALL, seed=seed), "the Queen's hall"
        run.checkpoint("queens_hall")
    if ahead("dalton"):
        # Up the hall to the throne; his scene, like Belthasar's, waits on a
        # press with no window open.
        for _ in range(60):
            machine.run(8, "UP")
            if run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        for _ in range(200):
            if A.battle_menu_open(machine):
                break
            machine.run(3, "A")
            machine.run(37)
        assert A.battle_menu_open(machine), "Dalton's battle"
        run.checkpoint("dalton")
    if ahead("dalton_won"):
        playthrough.win(run, seed, dalton_policy)
        run.checkpoint("dalton_won")


def ocean_palace(run, ahead, seed):
    """Into the Ocean Palace after Dalton, down to Masa and Mune's landing."""
    machine = run.machine
    if ahead("ocean_palace"):
        # His scene ends at the throne with the Gate he opened above it.
        assert stuck_in_hand(run, seed=seed), "the Queen's hall in hand"
        assert run.talk(OCEAN_GATE, done=lambda: run.map_id() != QUEENS_HALL), "the Gate to the Ocean Palace"
        assert in_hand(run), "the palace's entrance"
        assert leave_fighting(run, PALACE_HALL, seed=seed), "its main hall"
        run.checkpoint("ocean_palace")
    if ahead("first_button"):
        # The east wing's button, past its first fight, calls out the guards.
        take_exit(run, *HALL_EAST_DOOR, seed)
        x, y = EAST_WING_DOOR
        assert walk_fighting(run, lambda px, py: abs(px - x) < 12 and abs(py - y) < 12, seed), "the wing's inner door"
        machine.run(20, "UP")
        machine.run(20, "LEFT")
        assert walk_fighting(run, lambda px, py: machine.b(PALACE_BUTTONS) & 0x04, seed), "the east wing's button"
        run.checkpoint("first_button")
    if ahead("second_button"):
        assert leave_fighting(run, PALACE_HALL, seed=seed), "back to the main hall"
        take_exit(run, *HALL_SOUTH_EAST_DOOR, seed)
        assert walk_fighting(run, lambda px, py: machine.b(PALACE_BUTTONS) & 0x01, seed), "the bridge's button"
        run.checkpoint("second_button")
    if ahead("palace_stairs"):
        # The two buttons opened the hall's way down.
        assert leave_fighting(run, PALACE_HALL, max_rounds=20, seed=seed), "back to the main hall"
        take_exit(run, *HALL_DOWN, seed)
        assert run.map_id() == PALACE_DOWN
        assert leave_fighting(run, PALACE_STAIRS, seed=seed), "the stairs"
        run.checkpoint("palace_stairs")
    if ahead("platforms"):
        # The statues on the platforms keep taking the field's control a
        # moment at a time (field_player_control) while the pad still walks
        # the party, so this stretch walks by position and fights what
        # opens a battle, without waiting for the control to settle.
        assert in_hand(run), "the stairs' top in hand"
        for depth in range(200, PLATFORMS_TOP + 1, 100):
            for _ in range(30):
                if run.me()[1] > depth:
                    break
                for _ in range(60):         # the next moment the pad has the party
                    if machine.b(A.FIELD_PLAYER_CONTROL):
                        break
                    machine.run(1)
                run.find_player()
                moves = run.walk_here(lambda m, px, py: py > depth or A.battle_menu_open(m), max_nodes=3000)
                if moves is None:           # a monster in the way: walk into it
                    moves = run.walk_here(lambda m, px, py: py > depth or not m.b(A.FIELD_PLAYER_CONTROL),
                                          max_nodes=3000)
                run.say("stairs", depth, "steps", moves and len(moves), "at", run.me())
                # A monster's touch opens a battle a moment later.
                for _ in range(9):
                    if early.battle_ready(run):
                        win(run, seed)
                        break
                    machine.run(10)
        assert run.me()[1] > PLATFORMS_TOP, "down the stairs"
        run.checkpoint("platforms")
    if ahead("masa_and_mune"):
        x, y = CLOGGED_STAIRS
        run.find_player()
        assert run.walk_here(lambda m, px, py: py > y - 6 and abs(px - x) < 16) is not None, "the clogged stairs"
        for _ in range(30):
            machine.run(10, "DOWN")
            if not machine.b(A.FIELD_PLAYER_CONTROL) and run.me()[1] > y:
                break
        # The statues leap to life around the platform: a fight, then the
        # way down is clear to the landing, where Masa and Mune wait.
        for _ in range(200):
            if A.battle_menu_open(machine):
                win(run, seed)
                continue
            if run.dialog():
                machine.run(3, "A")
            machine.run(40)
            if machine.b(A.FIELD_PLAYER_CONTROL) and run.find_player() is not None:
                break
        assert leave_fighting(run, PALACE_LANDING, seed=seed), "the landing"
        assert machine.b(STORY) == PALACE_LANDED, "Masa and Mune met"
        run.checkpoint("masa_and_mune")
    if ahead("palace_rested"):
        assert in_hand(run), "the landing in hand"
        approach(run, *PALACE_SAVE_SPOT)
        rest(run)
        run.checkpoint("palace_rested")


def palace_floor(run, ahead, seed):
    """Down the elevator to the palace's floor, its two switches, and a rest
    at the save point before the Golem Twins."""
    machine = run.machine
    if ahead("elevator"):
        assert leave_fighting(run, ELEVATOR, seed=seed), "the elevator"
        assert in_hand(run)
        # Onto the platform: monsters drop onto it as it starts.
        for _ in range(20):
            machine.run(8, "DOWN")
            if not machine.b(A.FIELD_PLAYER_CONTROL) or run.me()[1] > ELEVATOR_ROW * 16:
                break
        assert in_hand(run, 30000, seed), "the first fight on the elevator"
        # After that fight the elevator stands still until the orb on its
        # left is confirmed. The original waits there too: checked with the
        # frozen emulator-oracle from ctsave10 slot 2, where the platform
        # stays put for 25000 frames after the first fight.
        x, y = ELEVATOR_DOWN
        assert run.walk(lambda m, px, py: abs(px - x) <= 3 and abs(py - y) <= 3) is not None, "beside the orb"
        machine.run(1, "LEFT")
        machine.run(5)
        machine.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == ELEVATOR_SHAFT, 600), "the elevator going down"
        # Down the shaft, fighting what lands on the platform, to its foot.
        for _ in range(3000):
            if early.battle_ready(run):
                win(run, seed)
                continue
            if run.dialog():
                machine.run(3, "A")
            machine.run(20)
            if run.map_id() == ELEVATOR_FOOT and machine.b(A.FIELD_PLAYER_CONTROL) and run.find_player() is not None:
                break
        assert run.map_id() == ELEVATOR_FOOT, "the elevator's foot"
        run.checkpoint("elevator")
    if ahead("palace_floor"):
        assert leave_fighting(run, PALACE_FLOOR_MAP, seed=seed), "the palace's floor"
        assert in_hand(run)
        run.checkpoint("palace_floor")
    for phase, ((column, row), (x, y), bit) in zip(("east_switch", "west_switch"), FLOOR_SWITCHES):
        if not ahead(phase):
            continue
        take_exit(run, column, row, "UP", seed)
        assert walk_fighting(run, lambda px, py: abs(px - x) < 6 and py <= y + 4, seed), "below the switch"
        machine.run(2, "UP")
        machine.run(6)
        machine.run(3, "A")
        machine.run(60)
        assert machine.b(PALACE_BUTTONS) & bit, "the switch"
        # Its guards, where they still stand on the walkway, come for the party.
        for _ in range(60):
            if early.battle_ready(run):
                win(run, seed)
                break
            machine.run(10, "DOWN")
        assert leave_fighting(run, PALACE_FLOOR_MAP, seed=seed), "back on the floor"
        run.checkpoint(phase)
    if ahead("twins_rested"):
        # The floor's button, with both switches on, lets down the bridge.
        assert leave_fighting(run, TWINS_SAVE_ROOM, seed=seed), "over the bridge"
        assert in_hand(run)
        approach(run, *TWINS_SAVE_SPOT)
        rest(run)
        run.checkpoint("twins_rested")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        ahead = lambda name: start <= PHASES.index(name)

        def within(phases):
            """Where another route's stretch starts in this run (its `play`'s
            `start`), or None when the run resumes past it."""
            first = PHASES.index(phases[0])
            return max(1, start - first + 1) if start < first + len(phases) else None

        if ahead("loaded"):
            load(run)
            run.checkpoint("loaded")
        if within(TYRANO):
            black_tyrano.play(run, within(TYRANO), args.seed)
        if ahead("lair_ruins"):
            # Ayla's farewell to her people, then the plateau: the crater
            # north of it is the Lair Ruins.
            assert world_in_hand(run, WORLD_65M), "the 65,000,000 B.C. map in hand"
            world_enter(run, LAIR_RUINS)
            assert run.wait_control(), "the party in the Lair Ruins"
            run.checkpoint("lair_ruins")
        if ahead("gate_12kbc"):
            # The Gate the fall opened goes to 12000 B.C.: a cave on the snowfield.
            assert run.talk(LAIR_RUINS_GATE, done=lambda: run.map_id() != LAIR_RUINS_MAP), "the Gate"
            assert run.wait_control(), "the party in the cave"
            assert run.map_id() == CAVE_12KBC and machine.b(STORY) == 0x9F, "12000 B.C."
            run.checkpoint("gate_12kbc")
        if ahead("snowfield"):
            assert run.exit_to(SNOWFIELD), "out onto the snowfield"
            machine.run(120)
            run.checkpoint("snowfield")
        if within(ZEAL):
            zeal_palace.play(run, within(ZEAL))
        exile(run, ahead, args.seed)
        to_the_epoch(run, ahead, args.seed)
        if within(MUD_IMP):
            mud_imp.play(run, within(MUD_IMP), args.seed)
        mt_woe(run, ahead, args.seed)
        if within(GIGA_GAIA):
            giga_gaia.play(run, within(GIGA_GAIA), args.seed)
        to_dalton(run, ahead, args.seed)
        ocean_palace(run, ahead, args.seed)
        palace_floor(run, ahead, args.seed)
        if within(TWINS):
            golem_twins.play(run, within(TWINS), args.seed, twins_policy)
        if within(LAVOS):
            # From the Twins' room on: the Mammon Machine and Lavos rising.
            ocean_palace_lavos.play(run, within(LAVOS) + 1, len(LAVOS) + 1)
        run.finish("Lavos rising from the Mammon Machine")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
