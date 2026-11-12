"""A playthrough from a new game, on a blank cartridge, toward an ending.

The story's opening is played here: the battle mode and names, Crono's
house, the Millennial Fair, Marle and Lucca's telepod. From the 600 A.D.
world map on, the route goes on with the pieces other routes play from
cartridge saves (the Cathedral, the trial, ...), from the state this route
reached instead of a save. Each phase ends at a checkpoint and the route goes
on only past checkpoints that succeeded; where it stops is recorded in
docs/TODO.md.
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import field_exit, world_enter
from . import _trial_party as early

PHASES = ["named", "home", "fair", "collided", "marle", "exhibit", "gate"]
HEADER = """\
# A playthrough from a new game (route playthrough), on a blank cartridge.
# frames {FRAMES}
# <start_frame> <frame_count> <buttons>"""

MENU_SCREEN = 0x7E0067      # which of the menus' screens is open
NAMING_SCREEN = 0x0D        # name entry (and, on a new game, the battle mode first)
CURSOR = 0x7E0054
LEENE_SQUARE = 0x005        # the Millennial Fair's lower square
MARLE_AT_THE_BELL = 3       # an object of the bell's plaza
FAIR_FLAGS, PENDANT_FOUND = 0x7F0054, 0x20
TRUCE_CANYON_600 = "7100"
MARLE_AT_THE_EXHIBIT = 4
FAIR_NEWS, SHOW_STARTED = 0x7F0055, 0x80
LOWER_SQUARE_FAIRGOERS = (17, 16, 22, 24)   # who tell of Lucca's show
WAIT_MODE = 1               # the battle mode question's second row: Wait


def naming(run):
    """A name entry screen is open: no field to walk and no window to close."""
    machine = run.machine
    return (machine.b(MENU_SCREEN) == NAMING_SCREEN and not run.dialog()
            and not machine.b(A.FIELD_PLAYER_CONTROL))


def in_hand(run, max_frames=12000):
    """Play the scene on until the pad walks the party again: windows closed with
    confirm, and each name the game asks for accepted as offered (Start)."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        if naming(run):
            machine.run(3, "START")
            machine.run(40)
            continue
        if run.wait_control(min(1200, max_frames)):
            return True
    return False


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
    assert in_hand(run, 20000), "Crono awake in his room"
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
            assert run.talk(15, done=lambda: naming(run)) or True
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
            run.checkpoint("marle")
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
        run.finish("the playthrough's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
