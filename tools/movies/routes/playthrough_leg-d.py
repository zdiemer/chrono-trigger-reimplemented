"""Leg D of the playthrough: from the Blackbird, after Crono is gone, to an ending.

Save: ctsave11 slot 1 (fantasyanime archive; the party held in the Blackbird).

The main playthrough (playthrough.py) plays the early game from a blank
cartridge; the legs start from cartridge saves so the whole game is played in
parallel. This one plays the end of the game with the save's party and items:
the Blackbird's equipment recovery and Dalton (blackbird_save, dalton); Magus
at North Cape, who joins (north_cape), after which the winged Epoch flies and
the Black Omen rises; Gaspar's Chrono Trigger at the End of Time; silver
points from Gato and Bekkler's clone at the Millennial Fair (fair_clone);
Belthasar's message in the Keeper's Dome; Death Peak and Crono's return
(death_peak_wind, death_peak, death_peak_return); and, Crono in the party,
the bucket to the Day of Lavos and the fights inside it (other_endings).
Each phase ends at a checkpoint and the route goes on only past checkpoints
that succeeded; where another route already plays a stretch, its
`play(run, start)` plays it from the state this route reached. The Epoch's
flying is routes/_epoch.py's.
"""
from .. import addresses as A
from .. import policies
from ..policies import ETHER, FULL_ETHER, FULL_TONIC, MID_ETHER, MID_TONIC
from .. import other_endings
from ..campaign import Run, route_arguments, run_route
from . import blackbird_save
from . import dalton
from . import death_peak
from . import death_peak_return
from . import death_peak_wind
from . import _fair as fair
from . import fair_clone
from . import north_cape
from ._epoch import (CHOICE_ACTIVE, CHOICE_READY, CHOICE_ROW, DISEMBARK, END_OF_TIME_DOCK, PERIOD_1000, PERIOD_2300,
                     PERIOD_END_OF_TIME, board, dial, dial_from_the_dock, go_through_time, land_by, wait_until)
from ._trial_walk import world_enter
from .menu_sweep import EQUIPMENT, ITEMS, LAPIS, SHELTER, TECHS, back_to_main_menu, count_of, open_menu, open_option, use_item
from .save_point import press

SAVE = "saves/fantasyanime/ctsave11-crono_gone.srm"
BLACKBIRD = blackbird_save.PHASES
DALTON = dalton.PHASES[1:]
# Until Magus has been met at North Cape, the Epoch shows Marle's resolve
# instead of flying (story $D2); meeting him moves the story to $D3.
NORTH_CAPE = north_cape.PHASES[1:]
# The first flight through time is a scene: the Black Omen rises (story $D4).
TIME = ["omen_rises", "end_of_time", "chrono_trigger"]
# Belthasar's message asks for a clone: Norstein Bekkler makes one at the
# Millennial Fair for silver points (the party has 6; Gato pays 15 a win),
# and it is delivered to Crono's room. Then Belthasar, heard again with the
# clone in hand, sends the party up Death Peak.
CLONE = ["fair", "silver_points", "tent"]
CLONE_BACK = fair_clone.PHASES[2:]
KEEPER = ["clone", "keepers_dome", "belthasar"]
# Death Peak: the wind slope (death_peak_wind), the caves past two Lavos
# Spawn to the last save point (death_peak), the last Spawn and the summit,
# where the Chrono Trigger brings Crono back (death_peak_return).
TO_THE_PEAK = ["peak"]
WIND = death_peak_wind.PHASES[1:]
# The party that comes this far (levels 34-38) is no match for Lavos: its
# shell takes some 400 HP from each member a blow, and its inner form wipes a
# party at 45 before anyone acts (the fantasyanime player had theirs at 47-53
# for Lavos). It grows here, where the slope's top and the lower field meet:
# each crossing brings back the monsters by the way in, some 3,200
# experience a round, until everyone is at GRIND_LEVEL. It has to be now:
# the field above the slope is left only by the summit's scene, once, since
# its ledges drop one way (the original's own code stops at them too, played
# with the frozen emulator-oracle oracle from ctsave12 slot 1).
GRIND = ["lower_field", "grinding", "ground"]
GRIND_LEVEL = 58
# The growing heals from the bag (Lapis, tonics, Ethers for Robo's Heal
# Beam), and the field above the slope has no shop: it stops with enough left
# to reach the mountain's save point, where a Shelter restores everyone.
GRIND_RESERVE = 8
# Then Marle wears the Wallet, which pays each fight's experience as gold,
# until the purse holds PURSE. At stock_up's prices, in its order, 200,000
# buys two Aeon Suits and two Aeon Helms (33,600), Full Tonics to 99 (at most
# 69,300) and Mid Ethers with what is left (some 48 from an empty bag, 96,000);
# the Mid Tonics and Ethers after them take only the change. Filling the bag
# with all four to 99 from empty would cost about 390,000.
PURSE = 200000
CAVES = ["mountain_save"] + death_peak.PHASES[1:]
# The caves' two Spawn leave the party all but beaten, and the third, met
# so, wipes it before anyone acts: a Shelter at the last save point first.
SUMMIT = ["rested"] + death_peak_return.PHASES[1:]
# Crono back, he joins in Robo's place at the End of Time, grown with the
# others (the experience the party wins is shared with those in reserve).
# Lavos's inner form opens with a spell a member at full HP lives through
# with about 70 magic defence (computed, policies.lavos_inner): Crono, Marle
# and Lucca have it, Robo (67) does not. The purse the growing filled buys the 12000 B.C.
# village's best armour and helms and its Full and Mid Tonics and Ethers,
# before the End of Time's light and its bucket into Lavos's shell
# (other_endings). The shell's imitations fall to the party's techs (Crono's
# lightning for Nizbel), healed from the bag between them; the shell's own
# form to Haste and healing first.
REUNITED = ["restored", "crono_joins"]
GROWN = ["stocked", "equipped"]
LAVOS = other_endings.BUCKET_PHASES[1:3] + ["imitations"] + other_endings.BUCKET_PHASES[3:]
PHASES = (BLACKBIRD + DALTON + NORTH_CAPE + TIME + CLONE + CLONE_BACK + KEEPER + TO_THE_PEAK + WIND + GRIND + CAVES
          + SUMMIT + REUNITED + GROWN + LAVOS)
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# Leg D of the playthrough, from the Blackbird to an ending (route playthrough_leg-d).
# Save: ctsave11 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

STORY = A.EVENT_MEMORY
OMEN_RISEN = 0xD4
WORLD_12000 = "f601"
END_OF_TIME = "d001"
KEEPER_FLAGS = 0x7F007C         # bit 0 the Chrono Trigger given, bit 1 Belthasar heard with the clone
CHRONO_TRIGGER, CRONOS_CLONE = 0xD9, 0xE2
# Gaspar dozes under the lamp at the End of Time; he is spoken to from his
# left. The first talk is about Crono; called back as the party leaves, the
# second gives the Chrono Trigger.
BESIDE_GASPAR = (448, 360)
LEENE_SQUARE, BELL_PLAZA, GATOS_RING = 0x005, "b701", "0600"
GATO, GATO_POINTS = 8, 15       # the ring's object 8; each win
SILVER_POINTS = 0x7F0052        # the fair's points (tools/movies/routes/_fair.py)
CLONE_PRICE = 40
MENU_CURSOR = 0x7E0054
RESERVE_TOP, EXCHANGE_ROWS = 0x7E0080, 0x7E0F02   # the party exchange (menu/exchange.c)
ACTIVE_ROWS, ROW_PLACE = 3, 0x3F
CRONO, MARLE, LUCCA, ROBO = 0, 1, 2, 3   # character numbers
BELOW_CRONO = (472, 400)        # Crono at the End of Time once he is back, spoken to from below
EQUIPMENT_LIST = 0x7E0F00       # the equipment screen's list of what the slot can take
WEAPON, HELM, ARMOUR, ACCESSORY = 0, 1, 2, 3                 # its slots' rows
SLOT_RECORD = {HELM: 0x27, ARMOUR: 0x28, WEAPON: 0x29, ACCESSORY: 0x2A}   # character_records' gear
FIRST_LIST_ROW = 4                                           # its cursor on the list's first row
AEON_SUIT, AEON_HELM, RAGE_BAND = 0x69, 0x85, 0xA6
WALLET, MAGIC_SCARF = 0xA9, 0x99      # the accessory that turns a fight's experience into gold; Marle's own
AEON_SUITS, AEON_HELMS = 6, 7   # their places in the 12000 B.C. village shop's list
BEST_GEAR = [(MARLE, ARMOUR, AEON_SUIT), (MARLE, HELM, AEON_HELM), (LUCCA, ARMOUR, AEON_SUIT),
             (CRONO, HELM, AEON_HELM), (CRONO, ACCESSORY, RAGE_BAND)]
HEAL_BEAM, HEAL_BEAM_MP = 4, 3         # Robo's tech list's Heal Beam and its cost
FIRST_MEMBER_ROW = 11                          # the techs screen's cursor on the first member
GOLD, MENU_SCREEN = 0x7E2C53, 0x7E0067
SHOP_SCREEN, SHOP_BUY, SHOP_COUNT = 12, 8, 10   # the shop's screen, and its Buy row's and count's cursor
COMMONS_SHOP, COMMONS_MERCHANT = 0x1AA, 8      # the 12000 B.C. village's shop by the Earthbound Village
MID_TONICS, FULL_TONICS, ETHERS, MID_ETHERS = 9, 10, 14, 15   # their places in its list
PERIOD_12000 = 5
RESTORING_LIGHT = 12            # the End of Time's light by the stair: HP and MP restored
CHARACTER_RECORDS, CHARACTER_RECORD_BYTES = 0x7E2600, 0x50   # a word of HP at +3, its most at +5
LEVEL, EXPERIENCE = 0x12, 0x13  # character_records' fields (tools/movies/fight_facts.py)
CRONOS_HOUSE, CRONOS_HOUSE_DOWNSTAIRS, CRONOS_ROOM = 0x001, "0100", "0200"
DELIVERED_CLONE = 19            # Crono's room's object 19, the clone, once Bekkler has made it
KEEPERS_DOME, KEEPERS_HANGAR = 0x0F1, "f200"
WORLD_1000, WORLD_2300 = "f001", "f201"
BEFORE_THE_SEALED_DOOR = 330    # the dome's hall, below the door the pendant opens
DEATH_PEAK = 0x0F4             # its wind slope, entered from the 2300 A.D. map
DEATH_PEAK_SAVE_POINT = (856, 448)   # on the mountain's lower field ($0F6), where death_peak starts
BELTHASAR = 16                  # the hangar's object 16, his recorded message, spoken to from below


def has(machine, item):
    return item in machine.peek(A.INVENTORY_ITEMS, A.INVENTORY_SIZE)


def disembark_at_the_end_of_time(run):
    """Travelling to the End of Time: the docked Epoch asks Full speed ahead!
    or Disembark; no confirm until the choice takes the pad, since its first
    row would set off again."""
    machine = run.machine
    assert wait_until(machine, lambda: run.map_id() == END_OF_TIME_DOCK
                      and machine.b(CHOICE_ACTIVE) == CHOICE_READY, 8000), "docked at the End of Time"
    machine.run(60)
    machine.run(3, "DOWN")
    machine.run(20)
    assert machine.b(CHOICE_ROW) == DISEMBARK, "Disembark"
    machine.run(3, "A")
    assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME, 3000), "the End of Time"
    assert run.wait_control(), "the End of Time in hand"


def restore_at_the_light(run):
    """The End of Time's light restores the party's HP and MP."""
    machine = run.machine
    assert run.talk(RESTORING_LIGHT), "the End of Time's light"
    for (hp, most), (mp, most_mp) in zip(hit_points(machine), magic_points(machine)):
        assert hp == most and mp == most_mp, "restored"


def speak_to_gaspar(run):
    """From his left, facing him, confirm until he answers; then hear him out."""
    machine = run.machine
    assert run.wait_control(), "the End of Time in hand"
    assert run.go(*BESIDE_GASPAR, tolerance=3), "beside Gaspar"
    machine.run(2, "RIGHT")
    machine.run(8)
    for _ in range(6):
        machine.run(3, "A")
        machine.run(40)
        if run.dialog():
            break
    assert run.dialog(), "Gaspar answers"
    assert run.wait_control(), "Gaspar's talk over"


def decline_the_epoch(run):
    """The dock asks whether to board the Epoch: not yet (No, the second row)."""
    machine = run.machine
    if machine.b(CHOICE_ACTIVE) != CHOICE_READY:
        machine.run(30)
    if machine.b(CHOICE_ACTIVE) == CHOICE_READY:
        machine.run(3, "DOWN")
        machine.run(20)
        machine.run(3, "A")
        machine.run(40)


def won_against_all(run, state):
    """A fight's end: every enemy down and someone standing."""
    return not policies.enemies(state) and any(state.hp(slot) for slot in range(3))


def fight_through(run, arrived, what, seed=1, rounds=8):
    """Walk until `arrived(machine, x, y)`, winning the fights that the
    field's monsters start on the way (the walk stops where one blocks it)."""
    machine = run.machine
    here = lambda: arrived(machine, *run.me())
    for _ in range(rounds):
        assert run.wait_control(), "the field in hand"
        if here():
            return
        moves = run.walk(lambda m, px, py: arrived(m, px, py) or not m.b(A.FIELD_PLAYER_CONTROL))
        run.say("fight_through", what, "steps", moves and len(moves), "now", run.map_id(), "at", run.me())
        assert moves is not None, ("a way to", what)
        machine.run(60)
        if run.dialogs_until(lambda: A.battle_menu_open(machine) or bool(machine.b(A.FIELD_PLAYER_CONTROL)),
                             3000) and A.battle_menu_open(machine):
            run.fight("party", won_against_all, party=3, seed=seed)
    assert run.wait_control() and here(), ("arrived at", what)


def hit_points(machine):
    """Each member's HP and its most."""
    records = [CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES for member in machine.peek(A.PARTY_MEMBERS, 3)]
    return [(machine.w(record + 3), machine.w(record + 5)) for record in records]


def magic_points(machine):
    """Each member's MP and its most."""
    records = [CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES for member in machine.peek(A.PARTY_MEMBERS, 3)]
    return [(machine.w(record + 7), machine.w(record + 9)) for record in records]


def patch_up(run, well=0.75, mp_well=None):
    """Away from a save point: Lapis from the items, which heals everyone some
    200 HP, until no member is below `well` of their HP, while there are any;
    then a Full Tonic (500 HP) or a Mid Tonic for each member still below. With
    `mp_well`, an Ether for each member below that of their MP (a Full Ether
    where much is missing)."""
    machine = run.machine
    low = lambda: [hp < well * most for hp, most in hit_points(machine)]
    short = lambda: [mp_well is not None and mp < mp_well * most for mp, most in magic_points(machine)]
    if not any(low()) and not any(short()):
        return
    assert run.wait_control(), "the field in hand"
    open_menu(machine)
    open_option(machine, ITEMS)
    for _ in range(8):
        lows = low()
        if not any(lows):
            break
        if sum(lows) < 2 or not count_of(machine, LAPIS):
            break
        use_item(run, LAPIS, choose_member=True)
    for place, (hp, most) in enumerate(hit_points(machine)):
        for _ in range(4):
            hp, most = hit_points(machine)[place]
            if hp >= well * most:
                break
            kinds = (FULL_TONIC, MID_TONIC) if most - hp > 250 else (MID_TONIC, FULL_TONIC)
            item = next((kind for kind in kinds if count_of(machine, kind)), None)
            if item is None:
                break
            use_item(run, item, choose_member=True, extra=("DOWN",) * place)
    for place, (mp, most) in enumerate(magic_points(machine)):
        if not short()[place]:
            continue
        kinds = (FULL_ETHER, MID_ETHER, ETHER) if most - mp > 40 else (MID_ETHER, ETHER, FULL_ETHER)
        item = next((kind for kind in kinds if count_of(machine, kind)), None)
        if item is not None:
            use_item(run, item, choose_member=True, extra=("DOWN",) * place)
    back_to_main_menu(machine)
    heal_with_beams(run, well)
    press(machine, "B", 120)
    assert run.wait_control(), "the field after patching up"
    run.say("patched up", hit_points(machine), magic_points(machine))


def heal_with_beams(run, well):
    """From the main menu, where the bag has nothing left to heal with: Robo's
    Heal Beam from the techs, everyone at once, while anyone is below `well`
    and he has the MP. The techs' member rows follow the party's order."""
    machine = run.machine
    party = list(machine.peek(A.PARTY_MEMBERS, 3))
    low = lambda: any(hp < well * most for hp, most in hit_points(machine))
    if ROBO not in party or not low():
        return
    open_option(machine, TECHS)
    machine.run(30)
    while machine.b(MENU_CURSOR) != FIRST_MEMBER_ROW + party.index(ROBO):
        press(machine, "DOWN", 30)
    press(machine, "A", 40)
    for _ in range(HEAL_BEAM):
        press(machine, "DOWN", 30)
    press(machine, "A", 60)
    robo = party.index(ROBO)
    for _ in range(12):
        if not low() or magic_points(machine)[robo][0] < HEAL_BEAM_MP:
            break
        press(machine, "A", 90)
    back_to_main_menu(machine)


def levels(machine):
    """The party's levels."""
    return [machine.b(CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES + LEVEL)
            for member in machine.peek(A.PARTY_MEMBERS, 3)]


def experience(machine):
    """The party's experience."""
    return [int.from_bytes(machine.peek(CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES + EXPERIENCE, 3),
                           "little") for member in machine.peek(A.PARTY_MEMBERS, 3)]


def fight_nearby(run, here, seed=1, reach=1500, most=3):
    """Fight the monsters the party can walk into within a short search on
    the maps `here`."""
    machine = run.machine
    for _ in range(most):
        assert run.wait_control(), "the field in hand"
        if run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) and m.peek(A.MAP_ID, 2).hex() in here,
                    max_nodes=reach) is None:
            return
        machine.run(60)
        if run.dialogs_until(lambda: A.battle_menu_open(machine) or bool(machine.b(A.FIELD_PLAYER_CONTROL)),
                             3000) and A.battle_menu_open(machine):
            run.fight("party_with_ethers", won_against_all, party=3, seed=seed)


def grind_a_round(run, seed=1):
    """A round of growing at Death Peak: the lower field's monsters by its way
    in, then over to the slope's top and its, and back; healed from the bag
    where two are hurt."""
    lower, upper = death_peak.LOWER_FIELD, death_peak_wind.LOWER
    fight_nearby(run, (lower, upper), seed)
    patch_up(run, 0.7, mp_well=0.4)
    assert run.exit_to(upper), "the slope's top"
    fight_nearby(run, (lower, upper), seed)
    patch_up(run, 0.7, mp_well=0.4)
    assert run.exit_to(lower), "the lower field"


def healing_left(machine):
    """The bag's Lapis, Full and Mid Tonics and Ethers, all told."""
    return sum(count_of(machine, item) for item in (LAPIS, FULL_TONIC, MID_TONIC, ETHER, MID_ETHER, FULL_ETHER))


def gold(machine):
    return int.from_bytes(machine.peek(GOLD, 3), "little")


def stock_up(run, merchant, purchases):
    """Speak to a merchant and buy each (place in the shop's list, the item,
    how many the bag should hold, the price), as many as the gold pays for
    and the bag's 99 of an item take: the list scrolls, so the
    cursor is counted from its top rather than read; the count starts at
    one, Up adds ten and Right one."""
    machine = run.machine
    assert run.talk(merchant, done=lambda: machine.b(MENU_SCREEN) == SHOP_SCREEN), "the shop"
    assert wait_until(machine, lambda: machine.b(MENU_CURSOR) == SHOP_BUY, 600), "the shop's Buy"
    machine.run(3, "A")
    machine.run(60)
    at = 0
    for place, item, want, price in purchases:
        count = max(0, min(want - count_of(machine, item), gold(machine) // price))
        if count == 0:
            continue
        while at != place:
            machine.run(3, "DOWN" if at < place else "UP")
            machine.run(30)
            at += 1 if at < place else -1
        machine.run(3, "A")
        assert wait_until(machine, lambda: machine.b(MENU_CURSOR) == SHOP_COUNT, 300), "the count"
        for _ in range((count - 1) // 10):
            machine.run(3, "UP")
            machine.run(20)
        for _ in range((count - 1) % 10):
            machine.run(3, "RIGHT")
            machine.run(20)
        machine.run(20)                     # the count takes no press at once
        before = gold(machine)
        machine.run(3, "A")
        machine.run(60)
        assert gold(machine) == before - count * price, ("bought", place, count)
    for _ in range(8):
        if machine.b(A.FIELD_PLAYER_CONTROL) and machine.b(MENU_SCREEN) != SHOP_SCREEN:
            break
        machine.run(3, "B")
        machine.run(60)
    assert run.wait_control(), "the field after the shop"


def rest_at_the_save_point(run):
    """Standing on a save point: a Shelter from the items restores everyone."""
    machine = run.machine
    assert count_of(machine, SHELTER), "a Shelter"
    assert run.wait_control(), "the field in hand"
    open_menu(machine)
    open_option(machine, ITEMS)
    use_item(run, SHELTER, choose_member=True)
    back_to_main_menu(machine)
    press(machine, "B", 120)
    assert run.wait_control(), "the field after the Shelter"
    for member in machine.peek(A.PARTY_MEMBERS, 3):
        record = CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES
        assert machine.w(record + 3) == machine.w(record + 5), "rested"


def the_shells_own_form(run, state):
    """Lavos's shell stands alone, its imitations beaten."""
    return policies.enemies(state) == [policies.LAVOS_SHELL] and state.max_hp(policies.LAVOS_SHELL) == policies.LAVOS_SHELL_HP


def press_until_choice(run):
    """Confirm until a choice takes the pad."""
    machine = run.machine
    for _ in range(6):
        machine.run(3, "A")
        machine.run(40)
        if machine.b(CHOICE_ACTIVE) == CHOICE_READY:
            return True
    return False


def member_under_the_cursor(machine):
    """Who the party exchange's cursor is on: its place is an active row, or
    one of the reserve's four shown from the reserve's top row."""
    place = machine.b(MENU_CURSOR)
    row = place + machine.b(RESERVE_TOP) if place >= ACTIVE_ROWS else place
    return machine.b(A.PARTY_MEMBERS + (machine.b(EXCHANGE_ROWS + row) & ROW_PLACE))


def exchange(run, joining, leaving):
    """On the party exchange's screen: choose the member `leaving` the active
    party, then the reserve's `joining`; the two change places."""
    machine = run.machine

    def choose(member, keys):
        for _ in range(8):
            if member_under_the_cursor(machine) == member:
                break
            machine.run(4, keys[0] if machine.b(MENU_CURSOR) < ACTIVE_ROWS else keys[1])
            machine.run(30)
        assert member_under_the_cursor(machine) == member, ("the exchange's cursor on", member)
        machine.run(4, "A")
        machine.run(60)
    choose(leaving, ("DOWN", "LEFT"))
    machine.run(4, "RIGHT")
    machine.run(30)
    choose(joining, ("RIGHT", "DOWN"))
    assert joining in machine.peek(A.PARTY_MEMBERS, 3), ("in the party", joining)
    for _ in range(6):
        if machine.b(A.FIELD_PLAYER_CONTROL):
            break
        machine.run(4, "B")
        machine.run(90)
    assert run.wait_control(), "the field after the exchange"


def gear(machine, member):
    """A member's weapon, helm, armour and accessory."""
    record = CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES
    return [machine.b(record + SLOT_RECORD[slot]) for slot in (WEAPON, HELM, ARMOUR, ACCESSORY)]


def equip(run, member, slot, item):
    """On the equipment screen: put `item` in the member's `slot`, from the
    list of what the slot can take; checked on the member's record."""
    machine = run.machine
    record = CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES + SLOT_RECORD[slot]
    if machine.b(record) == item:
        return
    assert run.wait_control(), "the field in hand"
    open_menu(machine)
    open_option(machine, EQUIPMENT)
    machine.run(30)
    for _ in range(list(machine.peek(A.PARTY_MEMBERS, 3)).index(member)):
        press(machine, "DOWN", 30)
    press(machine, "A", 40)
    for _ in range(slot):
        press(machine, "DOWN", 30)
    press(machine, "A", 40)
    row = list(machine.peek(EQUIPMENT_LIST, 32)).index(item)
    for _ in range(row):
        press(machine, "DOWN", 30)
    assert machine.b(MENU_CURSOR) == FIRST_LIST_ROW + row, ("the list's row", row)
    press(machine, "A", 60)
    back_to_main_menu(machine)
    press(machine, "B", 120)
    assert run.wait_control(), "the field after the equipment"
    assert machine.b(record) == item, ("equipped", member, slot, item)


def leave_the_end_of_time(run, period, world):
    """At the End of Time: board the Epoch at its dock and go to `period`,
    over `world`."""
    machine = run.machine
    run.walk(lambda m, px, py: m.b(CHOICE_ACTIVE) == CHOICE_READY)
    machine.run(3, "A")                     # Board the Epoch? Yes
    assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME_DOCK, 3000), "aboard at the dock"
    dial_from_the_dock(run, period)
    go_through_time(run, world)


def beat_gato(run, seed=1):
    """Gato, the singing robot at the fair: each win is 15 silver points."""
    machine = run.machine
    before = machine.b(SILVER_POINTS)
    assert run.wait_control(), "Gato's ring in hand"
    # His song, then the fight; a confirm pressed as it begins would open
    # the first member's target cursor, which the bot doesn't take for a menu.
    assert run.talk(GATO), "Gato's song"
    assert wait_until(machine, lambda: A.battle_menu_open(machine), 3000), "Gato's fight"
    run.fight("party", won_against_all, party=3, seed=seed)
    assert run.wait_control(), "the ring after the fight"
    run.say("Gato beaten; silver points", before, "->", machine.b(SILVER_POINTS))
    assert machine.b(SILVER_POINTS) == before + GATO_POINTS, "Gato's silver points"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    story = lambda: machine.b(STORY)
    try:
        start = run.start_at(PHASES)
        if start < len(BLACKBIRD):
            blackbird_save.play(run, start)
        later = start - len(BLACKBIRD)
        if later < len(DALTON):
            dalton.play(run, max(1, later + 1), args.seed)
        later -= len(DALTON)
        if later < len(NORTH_CAPE):
            north_cape.play(run, max(1, later + 1), args.seed, join=True)
        later -= len(NORTH_CAPE)
        if later <= 0:
            # The Epoch's first flight through time is caught by a Gate: the
            # Black Omen rises over the sea, and the party is back over the
            # 12000 B.C. map.
            assert run.exit_to(WORLD_12000), "off North Cape"
            # Its first flight is caught as it rises; were the scene not to
            # come, travel through time starts it.
            board(run, settle=False)
            if not run.dialogs_until(lambda: run.map_id() != WORLD_12000, 600):
                assert run.wait_world_control(), "the Epoch in hand"
                dial(run, PERIOD_END_OF_TIME)
                machine.run(4, "A")
            assert run.dialogs_until(lambda: story() == OMEN_RISEN and run.map_id() == WORLD_12000
                                     and not run.dialog(), 30000), "the Black Omen risen"
            run.wait_world_control(3000)
            run.say("Black Omen", "story", hex(story()), "at", run.world_pos())
            run.checkpoint("omen_rises")
        if later <= 1:
            # To the End of Time, where the docked Epoch's choice is to get out.
            dial(run, PERIOD_END_OF_TIME)
            machine.run(4, "A")
            disembark_at_the_end_of_time(run)
            run.checkpoint("end_of_time")
        if later <= 2:
            # Gaspar speaks of Crono; leaving, the party is called back, and
            # the second talk gives the Chrono Trigger.
            for _ in range(3):
                if has(machine, CHRONO_TRIGGER):
                    break
                speak_to_gaspar(run)
                if not has(machine, CHRONO_TRIGGER):
                    run.walk(lambda m, px, py: m.b(A.DIALOG_OPEN) != 0 or m.peek(A.MAP_ID, 2).hex() != END_OF_TIME)
                    decline_the_epoch(run)
                    assert run.wait_control(), "called back"
            assert has(machine, CHRONO_TRIGGER) and machine.b(KEEPER_FLAGS) & 1, "the Chrono Trigger"
            run.checkpoint("chrono_trigger")
        later -= len(TIME)
        if later <= 0:
            # Board the Epoch at the dock (Yes), Full speed ahead! to 1000
            # A.D., and come down by the fair.
            assert run.wait_control(), "the End of Time in hand"
            run.walk(lambda m, px, py: m.b(CHOICE_ACTIVE) == CHOICE_READY)
            machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME_DOCK, 3000), "aboard at the dock"
            dial_from_the_dock(run, PERIOD_1000)
            go_through_time(run, WORLD_1000)
            land_by(run, LEENE_SQUARE)
            assert run.wait_control(), "the fair in hand"
            run.checkpoint("fair")
        if later <= 1:
            # Gato, west of the bell's plaza, fights once a visit to his ring.
            assert run.exit_to(BELL_PLAZA), "the bell's plaza"
            while machine.b(SILVER_POINTS) < CLONE_PRICE:
                assert run.exit_to(GATOS_RING), "Gato's ring"
                beat_gato(run, args.seed)
                assert run.exit_to(BELL_PLAZA), "back to the plaza"
            run.checkpoint("silver_points")
        if later <= 2:
            # Out and in again by the fair's gate, the way to the tent.
            for want in (fair.SQUARE, WORLD_1000):
                assert run.exit_to(want), want
            run.wait_world_control()
            world_enter(run, LEENE_SQUARE)
            assert run.wait_control(), "the lower square in hand"
            fair.tent(run)
            run.checkpoint("tent")
        later -= len(CLONE)
        if later < len(CLONE_BACK):
            # With Crono gone, Bekkler's only game is the clone's: the first
            # row takes the challenge.
            assert run.wait_control(), "the tent in hand"
            fair_clone.play(run, 2 + max(0, later), challenge_row=0)
        later -= len(CLONE_BACK)
        if later <= 0:
            assert run.exit_to(WORLD_1000), "out of the fair"
            run.wait_world_control()
            world_enter(run, CRONOS_HOUSE)
            assert run.exit_to(CRONOS_ROOM), "upstairs"
            assert run.talk(DELIVERED_CLONE, done=lambda: has(machine, CRONOS_CLONE)), "the clone"
            run.checkpoint("clone")
        if later <= 1:
            for want in (CRONOS_HOUSE_DOWNSTAIRS, WORLD_1000):
                assert run.exit_to(want), want
            board(run)
            dial(run, PERIOD_2300)
            go_through_time(run, WORLD_2300)
            land_by(run, KEEPERS_DOME)
            assert run.wait_control(), "the Keeper's Dome in hand"
            run.checkpoint("keepers_dome")
        if later <= 2:
            # The pendant opens the hall's sealed door to the hangar, where
            # Belthasar's message plays.
            here = run.map_id()
            assert run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == here
                            and py < BEFORE_THE_SEALED_DOOR) is not None, "before the sealed door"
            machine.run(4, "UP")
            machine.run(10)
            machine.run(3, "A")
            assert run.wait_control(), "the door open"
            for _ in range(60):
                machine.run(4, "UP")
                if run.map_id() == KEEPERS_HANGAR:
                    break
            assert run.map_id() == KEEPERS_HANGAR and run.wait_control(), "the hangar"
            assert run.talk(BELTHASAR, done=lambda: machine.b(KEEPER_FLAGS) & 2), "Belthasar heard with the clone"
            run.checkpoint("belthasar")
        later -= len(KEEPER)
        if later <= 0:
            for want in ("f100", WORLD_2300):
                assert run.exit_to(want), want
            board(run)
            land_by(run, DEATH_PEAK, enter=False)
            run.checkpoint("peak")
        later -= len(TO_THE_PEAK)
        if later < len(WIND):
            death_peak_wind.play(run, max(1, later + 1))
        later -= len(WIND)
        lower, (x, y) = death_peak.LOWER_FIELD, DEATH_PEAK_SAVE_POINT
        if later <= 0:
            # On from the slope's top to the mountain's lower field, past the
            # monsters that stand in the way.
            fight_through(run, lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == lower, "the lower field", args.seed)
            run.checkpoint("lower_field")
        if later <= 2:
            equip(run, MARLE, ACCESSORY, MAGIC_SCARF)      # resumed with the Wallet on, it would earn no levels
            while min(levels(machine)) < GRIND_LEVEL and healing_left(machine) > GRIND_RESERVE:
                grind_a_round(run, args.seed)
                run.say("levels", levels(machine), "experience", experience(machine))
                machine.save_state("%s_grinding.st" % run.prefix)
            equip(run, MARLE, ACCESSORY, WALLET)
            while gold(machine) < PURSE:
                grind_a_round(run, args.seed)
                run.say("gold", gold(machine))
                machine.save_state("%s_grinding.st" % run.prefix)
            equip(run, MARLE, ACCESSORY, MAGIC_SCARF)
            run.checkpoint("ground")
        later -= len(GRIND)
        if later <= 0:
            fight_through(run, lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == lower and abs(px - x) <= 2
                          and abs(py - y) <= 2, "the save point", args.seed)
            rest_at_the_save_point(run)
            run.checkpoint("mountain_save")
        if later < len(CAVES):
            death_peak.play(run, max(1, later), args.seed, rest=patch_up)
        later -= len(CAVES)
        if later <= 0:
            rest_at_the_save_point(run)
            run.checkpoint("rested")
        if later < len(SUMMIT):
            death_peak_return.play(run, max(1, later), args.seed)
        later -= len(SUMMIT)
        if later <= 0:
            assert run.wait_control(), "the End of Time in hand"
            restore_at_the_light(run)
            run.checkpoint("restored")
        if later <= 1:
            assert run.wait_control() and run.find_player() is not None, "the End of Time in hand"
            assert run.go(*BELOW_CRONO, tolerance=3), "below Crono"
            machine.run(2, "UP")
            machine.run(8)
            assert press_until_choice(run), "Will you take Crono?"
            machine.run(30)
            machine.run(3, "A")                     # Yes
            machine.run(120)
            exchange(run, CRONO, ROBO)
            run.checkpoint("crono_joins")
        later -= len(REUNITED)
        if later <= 0:
            assert run.wait_control(), "the End of Time in hand"
            leave_the_end_of_time(run, PERIOD_12000, WORLD_12000)
            land_by(run, COMMONS_SHOP)
            assert run.wait_control(), "the shop in hand"
            stock_up(run, COMMONS_MERCHANT, [(AEON_SUITS, AEON_SUIT, 2, 9000), (AEON_HELMS, AEON_HELM, 2, 7800),
                                             (FULL_TONICS, FULL_TONIC, 99, 700), (MID_ETHERS, MID_ETHER, 99, 2000),
                                             (MID_TONICS, MID_TONIC, 99, 100), (ETHERS, ETHER, 99, 800)])
            run.checkpoint("stocked")
        if later <= 1:
            # The party's armour and helms, the best the shops have by now
            # (Aeon Suit and Aeon Helm), and Crono's Rage Band from the bag.
            for member, slot, item in BEST_GEAR:
                equip(run, member, slot, item)
            run.say("gear", [gear(machine, member) for member in machine.peek(A.PARTY_MEMBERS, 3)])
            assert run.exit_to(WORLD_12000), "out of the shop"
            board(run)
            dial(run, PERIOD_END_OF_TIME)
            machine.run(4, "A")
            disembark_at_the_end_of_time(run)
            restore_at_the_light(run)
            run.checkpoint("equipped")
        later -= len(GROWN)
        if later < 2:
            other_endings.bucket_to_shell(run, max(1, later + 1))
        # The bag is the middle game's (Lapis, Mid Tonics, Ethers); the party
        # is healed from it between the imitations.
        walk_on = other_endings.walking_into_lavos(run, lambda r: patch_up(r, 0.9, mp_well=0.6))
        if later <= 2:
            run.fight("lavos_shell", the_shells_own_form, party=3, seed=args.seed, idle=walk_on)
            run.checkpoint("imitations")
        if later <= 3:
            run.fight("lavos_shell", lambda r, s: r.map_id() == other_endings.INSIDE_LAVOS, party=3,
                      seed=args.seed, idle=walk_on)
            run.checkpoint("cavern")
        if later <= 4:
            # Inside the shell, before its inner form, whose first blow lands
            # before anyone acts: everyone healed and their MP back.
            assert run.wait_control(), "inside Lavos"
            patch_up(run, 0.95, mp_well=0.8)
        if later < len(LAVOS):
            other_endings.lavos(run, max(4, later + 1), party=3, seed=args.seed, policy="lavos_inner",
                                core_policy="lavos_core")
        later -= len(LAVOS)
        # Not reached yet: the core's fight wipes the party (docs/MOVIES.md).
        run.finish("Lavos beaten")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    except (AssertionError, RuntimeError):
        machine.save_state(args.prefix + "_failed.st")
        machine.screenshot(args.prefix + "_failed.png")
        run.say("failed at", machine.frame, "map", run.map_id())
        raise
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
