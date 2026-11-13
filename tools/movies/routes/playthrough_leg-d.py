"""Leg D of the playthrough: from the Blackbird, after Crono is gone, to an ending.

Save: ctsave11 slot 1 (fantasyanime archive; the party held in the Blackbird).

The main playthrough (playthrough.py) plays the early game from a blank
cartridge; the legs start from cartridge saves so the whole game is played in
parallel. This one plays the end of the game with the save's party and items:
the Blackbird's equipment recovery and Dalton (blackbird_save, dalton); Magus
at North Cape, whom the party lets join (north_cape), after which the winged
Epoch flies and the Black Omen rises; Gaspar's Chrono Trigger at the End of
Time; ... Each phase ends at a checkpoint and the route goes on only past
checkpoints that succeeded; where another route already plays a stretch, its
`play(run, start)` plays it from the state this route reached.
"""
from .. import addresses as A
from .. import bfs, policies
from ..campaign import Run, route_arguments, run_route
from . import blackbird_save
from . import dalton
from . import death_peak
from . import death_peak_return
from . import death_peak_wind
from . import _fair as fair
from . import fair_clone
from . import north_cape
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
PHASES = (BLACKBIRD + DALTON + NORTH_CAPE + TIME + CLONE + CLONE_BACK + KEEPER + TO_THE_PEAK + WIND + CAVES
          + SUMMIT)
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
END_OF_TIME, END_OF_TIME_DOCK = "d001", "d901"
TRAVEL_IN_THE_EPOCH = 2
DIAL_PERIOD = 0x7E0C52          # the period the time dial stands at (menu/epoch.c)
PERIOD_END_OF_TIME, PERIOD_2300, PERIOD_1000 = 0, 1, 3
CHOICE_ACTIVE, CHOICE_ROW = 0x7E0162, 0x7E0163
CHOICE_READY = 2                # the choice takes the pad
FULL_SPEED, DISEMBARK = 0, 1    # the docked Epoch's choice
KEEPER_FLAGS = 0x7F007C         # bit 0 the Chrono Trigger given, bit 1 Belthasar heard with the clone
CHRONO_TRIGGER, CRONOS_CLONE = 0xD9, 0xE2
# Gaspar dozes under the lamp at the End of Time; he is spoken to from his
# left. The first talk is about Crono; called back as the party leaves, the
# second gives the Chrono Trigger.
BESIDE_GASPAR = (448, 360)
WORLD_EXITS, WORLD_EXIT_BYTES, WORLD_EXIT_COUNT = 0x7E7E00, 7, 128   # the map's exit records
# Where to try to come down about an entrance, nearest first: the Epoch comes
# down only on open ground, which beside a town is often a few tiles off.
LANDING_SPOTS = sorted(((dx, dy) for dx in range(-96, 97, 16) for dy in range(-96, 97, 16)),
                       key=lambda d: abs(d[0]) + abs(d[1]))
LANDING_SLOT = 3990
LEENE_SQUARE, BELL_PLAZA, GATOS_RING = 0x005, "b701", "0600"
GATO, GATO_POINTS = 8, 15       # the ring's object 8; each win
SILVER_POINTS = 0x7F0052        # the fair's points (tools/movies/routes/_fair.py)
CLONE_PRICE = 40
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


def board(run):
    """On a world map on foot: walk up to the Epoch and board it."""
    machine = run.machine
    run.wait_world_control()
    moves, _ = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != 0,
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(), confirm=True, max_nodes=3000)
    assert moves is not None, "the Epoch to board"
    for _ in range(60):
        machine.run(10)
        if machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH:
            break
    assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH, "aboard the Epoch"
    # With its wings it rises first, and takes the pad once in the air.
    machine.run(30)
    assert run.wait_world_control(), "the Epoch in hand"


def dial(run, period):
    """In the Epoch: open the time dial (Y) and turn it to `period`."""
    machine = run.machine
    machine.run(4, "Y")
    machine.run(100)
    for _ in range(8):
        if machine.b(DIAL_PERIOD) == period:
            break
        machine.run(4, "UP" if machine.b(DIAL_PERIOD) > period else "DOWN")
        machine.run(56)
    assert machine.b(DIAL_PERIOD) == period, ("the dial at period", period)


def dial_from_the_dock(run, period):
    """In the Epoch docked at the End of Time: Full speed ahead! opens the dial."""
    machine = run.machine
    assert run.dialogs_until(lambda: machine.b(CHOICE_ACTIVE) == CHOICE_READY, 3000), "the Epoch's choice"
    machine.run(60)
    assert machine.b(CHOICE_ROW) == FULL_SPEED, "Full speed ahead!"
    machine.run(3, "A")
    machine.run(150)
    for _ in range(8):
        if machine.b(DIAL_PERIOD) == period:
            break
        machine.run(4, "UP" if machine.b(DIAL_PERIOD) > period else "DOWN")
        machine.run(56)
    assert machine.b(DIAL_PERIOD) == period, ("the dial at period", period)


def go_through_time(run, world):
    """Travel (A) with the dial set, through the departure's lines, until the
    party flies the Epoch over `world`."""
    machine = run.machine
    machine.run(4, "A")
    assert run.dialogs_until(lambda: run.map_id() == world, 8000), ("arrived over", world)
    run.wait_world_control()


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


def entrance(run, location):
    """Where a place's entrance is on this world map, from the map's exit records."""
    records = run.machine.peek(WORLD_EXITS, WORLD_EXIT_BYTES * WORLD_EXIT_COUNT)
    for offset in range(0, len(records), WORLD_EXIT_BYTES):
        record = records[offset:offset + WORLD_EXIT_BYTES]
        if record[0] & 0x80 and int.from_bytes(record[3:5], "little") & 0x1FF == location:
            return (record[0] & 0x7F) * 16, (record[1] & 0x3F) * 16
    raise AssertionError(("no entrance", hex(location)))


def fly_to(run, x, y, near=6):
    """In the air: steer the Epoch over (x, y)."""
    machine = run.machine
    for _ in range(600):
        px, py = run.world_pos()
        if abs(px - x) <= near and abs(py - y) <= near:
            return
        across = "" if abs(px - x) <= near else ("RIGHT" if px < x else "LEFT")
        down = "" if abs(py - y) <= near else ("DOWN" if py < y else "UP")
        machine.run(4, "+".join(b for b in (down, across) if b))
    raise AssertionError(("flying to", (x, y), "stuck at", run.world_pos()))


def land_by(run, location, enter=True):
    """Fly over a place's entrance, come down on open ground beside it, and
    walk in (unless not `enter`)."""
    machine = run.machine
    x, y = entrance(run, location)
    for dx, dy in LANDING_SPOTS:
        fly_to(run, x + dx, y + dy)
        machine.run(20)
        machine.mark(LANDING_SLOT)
        machine.run(3, "A")
        for _ in range(40):
            machine.run(10)
            if machine.b(A.OVERWORLD_TRAVEL) == 0:
                break
        if machine.b(A.OVERWORLD_TRAVEL) == 0:
            machine.free(LANDING_SLOT)
            break
        machine.restore(LANDING_SLOT)
        machine.free(LANDING_SLOT)
    assert machine.b(A.OVERWORLD_TRAVEL) == 0, ("landed by", hex(location))
    run.wait_world_control()
    if enter:
        world_enter(run, location)


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
        run.finish("leg D's last checkpoint")
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
