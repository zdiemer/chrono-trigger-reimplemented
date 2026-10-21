"""Save at the End of Time's save point: its own save screen, a slot kept and another saved over.

Save: ctsave12 slot 2 (fantasyanime archive; the party stands on the End of Time's save point, $1D0).

The save point is an object of the End of Time's script (object $0D, at tile $18,$16): while the
leader stands on its tile, pressing confirm asks the field for the menus' save point mode (event
$C8 $40), which opens the save screen on its own (menu screen 11, $C2:D4F8), over the main menu's
backdrop, rather than the main menu's Save, which other movies take. The route chooses the first
slot and answers no to saving over it, then the third and answers yes, and goes back to the field.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Save at the End of Time's save point, from its own save screen: load the save (the party stands on the save point), press
# confirm on it, choose the first slot and answer no to erasing its game, choose the third and answer yes, then go back to
# the field (route save_point).
# Save: ctsave12 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave12-side_quests1.srm"
SLOT = 1
PHASES = ["loaded", "opened", "saved", "back"]

END_OF_TIME = "d001"
MENU_SCREEN = 0x7E0067          # menu_screen
CURSOR = 0x7E0054               # menu_cursor_index: the slot, on the save screen
SAVE_POINT_SCREEN = 11          # MENU_SCREEN_SAVE_POINT


def press(machine, button, gap=30, hold=3):
    machine.run(hold, button)
    machine.run(gap)


def to_slot(machine, slot):
    for _ in range(4):
        if machine.b(CURSOR) == slot:
            break
        press(machine, "DOWN" if machine.b(CURSOR) < slot else "UP")
    assert machine.b(CURSOR) == slot, "the cursor on slot %d" % (slot + 1)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        run.select_slot(SLOT)
        machine.run(4, "A")
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        assert run.wait_control(), "the party in hand at the End of Time"
        run.say("loaded map", run.map_id(), "frame", machine.frame, "leader at", run.me())
        assert run.map_id() == END_OF_TIME, "the End of Time"
        run.checkpoint("loaded")

    if start <= 1:
        press(machine, "A", 90)
        assert machine.b(MENU_SCREEN) == SAVE_POINT_SCREEN, "the save point's save screen"
        run.checkpoint("opened")

    if start <= 2:
        to_slot(machine, 0)
        press(machine, "A", 60)                 # the first slot: erase its game?
        press(machine, "DOWN", 30)              # no
        press(machine, "A", 60)
        to_slot(machine, 2)
        press(machine, "A", 60)                 # the third: erase its game? yes
        press(machine, "A", 90)                 # saved, the slot's summary read back
        run.checkpoint("saved")

    if start <= 3:
        press(machine, "B", 30)                 # the menus closed, back to the field
        for _ in range(20):
            if run.find_player() is not None:
                break
            machine.run(10)
        assert run.find_player() is not None, "the party in hand again"
        run.checkpoint("back")
    run.say("end frame", machine.frame)
    run.finish("a save from the End of Time's save point")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
