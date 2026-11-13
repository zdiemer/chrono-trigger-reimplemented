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
                     PERIOD_END_OF_TIME, board, dial, dial_from_the_dock, go_through_time, land_by)
from ._trial_walk import world_enter
from .menu_sweep import ITEMS, SHELTER, back_to_main_menu, count_of, open_menu, open_option, use_item
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
CAVES = ["mountain_save"] + death_peak.PHASES[1:]
# The caves' two Spawn leave the party all but beaten, and the third, met
# so, wipes it before anyone acts: a Shelter at the last save point first.
SUMMIT = ["rested"] + death_peak_return.PHASES[1:]
# Crono back, the bucket at the End of Time leads to the Day of Lavos and
# into Lavos's shell (other_endings), whose imitations of the bosses before
# it this party beats one after another. The shell's own form (10000 HP,
# blows of 400-500 HP on each member) is more than it can take at levels
# 34-38: the leg ends where that form stands.
# Crono joins first, in Lucca's place: the shell's Nizbel imitation gives
# way only to his lightning. The End of Time's light restores the party.
REUNITED = ["crono_joins"]
LAVOS = other_endings.BUCKET_PHASES[1:3] + ["imitations"]
PHASES = (BLACKBIRD + DALTON + NORTH_CAPE + TIME + CLONE + CLONE_BACK + KEEPER + TO_THE_PEAK + WIND + CAVES
          + SUMMIT + REUNITED + LAVOS)
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
MENU_CURSOR, RESERVE_TOP, EXCHANGE_ROWS = 0x7E0054, 0x7E0080, 0x7E0F02   # the party exchange (menu/exchange.c)
ACTIVE_ROWS, ROW_PLACE = 3, 0x3F
CRONO, LUCCA = 0, 2             # character numbers
CRONO_AT_THE_GATE = 29          # the End of Time's object 29 once he is back, spoken to from below
BELOW_CRONO = (472, 400)
SHELL_SLOT, SHELL_HP = 3, 10000
RESTORING_LIGHT = 12            # the End of Time's light by the stair: HP and MP restored
CHARACTER_RECORDS, CHARACTER_RECORD_BYTES = 0x7E2600, 0x50   # a word of HP at +3, its most at +5
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
        if run.dialogs_until(lambda: A.battle_menu_open(machine) or bool(machine.b(A.FIELD_PLAYER_CONTROL)), 3000)                 and A.battle_menu_open(machine):
            run.fight("party", won_against_all, party=3, seed=seed)
    assert run.wait_control() and here(), ("arrived at", what)


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
    return policies.enemies(state) == [SHELL_SLOT] and state.max_hp(SHELL_SLOT) == SHELL_HP


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


def beat_gato(run, seed=1):
    """Gato, the singing robot at the fair: each win is 15 silver points."""
    machine = run.machine
    before = machine.b(SILVER_POINTS)
    assert run.wait_control(), "Gato's ring in hand"
    assert run.talk(GATO, done=lambda: A.battle_menu_open(machine)) or run.dialogs_until(
        lambda: A.battle_menu_open(machine), 3000), "Gato's fight"
    assert run.dialogs_until(lambda: A.battle_menu_open(machine), 3000), "Gato's fight"
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
            board(run)
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
            assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME_DOCK
                                     and machine.b(CHOICE_ACTIVE) == CHOICE_READY, 8000), "docked at the End of Time"
            machine.run(60)
            machine.run(3, "DOWN")
            machine.run(20)
            assert machine.b(CHOICE_ROW) == DISEMBARK, "Disembark"
            machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME, 3000), "the End of Time"
            assert run.wait_control(), "the End of Time in hand"
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
        if later <= 0:
            # On from the slope's top to the mountain's save point, past the
            # monsters that stand in the way.
            lower, (x, y) = death_peak.LOWER_FIELD, DEATH_PEAK_SAVE_POINT
            fight_through(run, lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == lower, "the lower field", args.seed)
            fight_through(run, lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == lower and abs(px - x) <= 2
                          and abs(py - y) <= 2, "the save point", args.seed)
            run.checkpoint("mountain_save")
        if later < len(CAVES):
            death_peak.play(run, max(1, later), args.seed)
        later -= len(CAVES)
        if later <= 0:
            rest_at_the_save_point(run)
            run.checkpoint("rested")
        if later < len(SUMMIT):
            death_peak_return.play(run, max(1, later), args.seed)
        later -= len(SUMMIT)
        if later <= 0:
            assert run.wait_control(), "the End of Time in hand"
            assert run.go(*BELOW_CRONO, tolerance=3), "below Crono"
            machine.run(2, "UP")
            machine.run(8)
            assert press_until_choice(run), "Will you take Crono?"
            machine.run(30)
            machine.run(3, "A")                     # Yes
            machine.run(120)
            exchange(run, CRONO, LUCCA)
            assert run.talk(RESTORING_LIGHT), "the End of Time's light"
            for member in machine.peek(A.PARTY_MEMBERS, 3):
                record = CHARACTER_RECORDS + member * CHARACTER_RECORD_BYTES
                assert machine.w(record + 3) == machine.w(record + 5), "restored"
            run.checkpoint("crono_joins")
        later -= len(REUNITED)
        if later < 2:
            other_endings.bucket_to_shell(run, later + 1)
        if later <= 2:
            # Between imitations the party walks into Lavos again. The bag is
            # the middle game's: Lapis, Mid Tonics and Ethers.
            run.fight("party_with_ethers", the_shells_own_form, party=3, seed=args.seed,
                      idle=lambda bot: bot.step(22, "UP"))
            run.checkpoint("imitations")
        later -= len(LAVOS)
        run.finish("Lavos's imitations beaten, its shell's own form reached")
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
