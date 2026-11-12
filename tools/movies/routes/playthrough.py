"""A playthrough from a new game, on a blank cartridge, toward an ending.

The story's opening is played here: the battle mode and names, Crono's
house, the Millennial Fair, Marle and Lucca's telepod. From the 600 A.D.
world map on, the route goes on with the pieces other routes play from
cartridge saves (the Cathedral, the trial, ...), from the state this route
reached instead of a save. Each phase ends at a checkpoint and the route goes
on only past checkpoints that succeeded. It ends with the Guardian beaten in
Arris Dome, 2300 A.D.
"""
from .. import addresses as A
from .. import policies
from .menu_sweep import SHELTER, count_of
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import field_exit, toward, world_enter
from . import _trial_party as early
from . import _trial_cathedral as cathedral
from . import cronos_trial
from . import dragon_tank
from . import guardian

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
PHASES = OPENING + CATHEDRAL + cronos_trial.PHASES + ESCAPE + TANK + FUTURE + GUARDIAN
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# A playthrough from a new game (route playthrough), on a blank cartridge.
# frames {FRAMES}
# <start_frame> <frame_count> <buttons>"""

MENU_SCREEN = 0x7E0067      # which of the menus' screens is open
NAMING_SCREEN = 0x0D        # name entry (and, on a new game, the battle mode first)
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
LAB16_WEST, LAB16_EAST = "d400", "d500"
LAB16_DOOR = (264, 368)     # on the 2300 A.D. map, north of Bangor Dome
LAB16_WEST_EXIT = (504, 120)   # tile column 31, rows 5-9, to the east part
LAB16_EAST_EXIT = (952, 536)   # tile columns 57-61, row 33, out by Arris Dome
ARRIS_DOME = 0x0D6
FIELD_PARTY_OBJECTS = 0x7E0197   # the members' object slots, times two
WAIT_MODE = 1              # the battle mode question's second row: Wait


def naming(run):
    """A name entry screen is open: no field to walk and no window to close."""
    machine = run.machine
    return (machine.b(MENU_SCREEN) == NAMING_SCREEN and not run.dialog()
            and not machine.b(A.FIELD_PLAYER_CONTROL))


def foes_with_hit_points(machine):
    """Whether the open battle has an enemy the bot can wear down: Lab 16's
    Shadows have 1 HP, as the battle's empty slots do, and take no harm from
    a weapon, so policies.enemies counts none of them."""
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


def win(run, seed=1):
    """Fight the battle that is open to its end: every enemy down, or the
    field given back (a fight a scene ends, as the prison's guards' does,
    leaves its enemies standing). A fight with nothing the bot can hurt is
    run from."""
    machine = run.machine
    if not foes_with_hit_points(machine):
        flee(run)
        return
    seen, field_frames = [], [0]

    def over(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        if seen and not living and any(state.hp(slot) for slot in range(party_size(r))):
            return True
        field_frames[0] = field_frames[0] + 1 if machine.b(A.FIELD_PLAYER_CONTROL) else 0
        return field_frames[0] >= 60

    run.fight(early.early_party, over, party=party_size(run), seed=seed, max_frames=35000, max_tries=300)


def in_hand(run, max_frames=12000, seed=1):
    """Play on until the pad walks the party again: windows closed with
    confirm, each name the game asks for accepted as offered (Start), and any
    fight that starts on the way won. True once it is."""
    machine = run.machine
    start = machine.frame
    found = held = 0
    while machine.frame - start < max_frames:
        if early.battle_ready(run):
            win(run, seed)
            found = held = 0
            continue
        if naming(run):
            machine.run(3, "START")
            machine.run(40)
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
    leader = run.machine.w(FIELD_PARTY_OBJECTS)
    assert leader < 0x80, "a leader on the field"
    run.player = leader // 2
    run.say("the leader is object", run.player)


def leave_fighting(run, want, max_rounds=12, seed=1, ends=()):
    """Walk until the screen is map `want`, winning the fights on the way (the
    field's encounters take control from the walk) and closing windows. A
    scene that carries the party on to one of the maps in `ends` ends the
    walk there too."""
    machine = run.machine
    done = lambda: run.map_id() == want or run.map_id() in ends
    for _ in range(max_rounds):
        assert in_hand(run), "the field in hand"
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
            win(run, seed)
    return done()


def through(run, x, y, arrived, seed=1, rounds=10):
    """Head for (x, y) by the biased search (_trial_walk.toward) until
    `arrived()`, settling the fights and scenes that stop the walk."""
    machine = run.machine
    for _ in range(rounds):
        assert in_hand(run), "the field in hand"
        if arrived():
            return
        toward(run, x, y, lambda: arrived() or not machine.b(A.FIELD_PLAYER_CONTROL) or run.dialog())
        if arrived():
            return
        assert in_hand(run, 30000, seed), "the scene or fight ends"
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
    for _ in range(6):
        if run.dialog() or machine.b(A.FIELD_PLAYER_CONTROL):
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
        run.finish("the playthrough's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
