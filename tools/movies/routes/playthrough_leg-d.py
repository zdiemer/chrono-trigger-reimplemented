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
from .. import bfs
from ..campaign import Run, route_arguments, run_route
from . import blackbird_save
from . import dalton
from . import north_cape

SAVE = "saves/fantasyanime/ctsave11-crono_gone.srm"
BLACKBIRD = blackbird_save.PHASES
DALTON = dalton.PHASES[1:]
# Until Magus has been met at North Cape, the Epoch shows Marle's resolve
# instead of flying (story $D2); meeting him moves the story to $D3.
NORTH_CAPE = north_cape.PHASES[1:]
# The first flight through time is a scene: the Black Omen rises (story $D4).
TIME = ["omen_rises", "end_of_time", "chrono_trigger"]
PHASES = BLACKBIRD + DALTON + NORTH_CAPE + TIME
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
CHRONO_TRIGGER, CLONE = 0xD9, 0xE2
# Gaspar dozes under the lamp at the End of Time; he is spoken to from his
# left. The first talk is about Crono; called back as the party leaves, the
# second gives the Chrono Trigger.
BESIDE_GASPAR = (448, 360)


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
    machine.run(30)


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
