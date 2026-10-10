"""Erase a slot on the load screen, then start a New Game+ from its fourth row.

Save: technoplaza ct-state16 (a finished cartridge with games in its first two
slots, so the fourth row offers New Game+ as well as a new game).
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Erase a slot, then start a New Game+ from the load screen: load the title; on the second slot, hold
# Select and Start on the second controller, answer no to the erase question, ask again and answer yes;
# then move the cursor down to the fourth row and right onto its second choice (New Game+), confirm,
# carry on from the first slot, accept the default Active battle mode and Crono's own name, then a few
# steps on the 1000 A.D. world map where it starts.
# Save: technoplaza ct-state16 (games.technoplaza.net/snes/chronotrigger/savestates.php, a finished cartridge).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/technoplaza/ct-state16.srm"
PHASES = ["loaded", "erased", "started"]

CURSOR = 0x7E0054          # the load screen's cursor: a slot 0-2, the fourth row 3, the erase question's yes 4 and no 5
SLOTS_IN_USE = 0x7E0D49    # a byte a slot, nonzero where the slot holds a game
ERASE_SLOT, FOURTH_ROW, YES, NO = 1, 3, 4, 5
ERASE = "P2:SELECT+P2:START"
MENU_SCREEN = 0x7E0067     # which of the menus' screens is open
NAMING_SCREEN = 0x0D       # name entry, which asks for the battle mode first on a new game


def ask_to_erase(run):
    machine = run.machine
    assert machine.b(CURSOR) == ERASE_SLOT, "the cursor starts on the slot saved last, the second"
    machine.run(3, ERASE)
    machine.run(30)                            # the question opens over the summary, the cursor on yes
    assert machine.b(CURSOR) == YES, "the erase question is asked"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              recomp=args.recomp, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        run.say("load screen, frame", machine.frame, "slots in use", machine.peek(SLOTS_IN_USE, 3).hex())
        run.checkpoint("loaded")
    if start <= 1:
        ask_to_erase(run)
        machine.run(3, "DOWN")
        machine.run(20)                        # onto no
        assert machine.b(CURSOR) == NO
        machine.run(3, "A")
        machine.run(30)                        # no: the question closes, the slot kept
        assert machine.b(SLOTS_IN_USE + ERASE_SLOT), "answering no keeps the slot's game"
        ask_to_erase(run)
        machine.run(3, "A")
        machine.run(40)                        # yes: the slot's game erased
        assert not machine.b(SLOTS_IN_USE + ERASE_SLOT), "answering yes erases the slot's game"
        run.say("erased slot", ERASE_SLOT + 1, "frame", machine.frame, "cursor", machine.b(CURSOR))
        run.checkpoint("erased")
    if start <= 2:
        for _ in range(4):
            if machine.b(CURSOR) == FOURTH_ROW:
                break
            machine.run(3, "DOWN")
            machine.run(20)
        assert machine.b(CURSOR) == FOURTH_ROW, "the cursor lands on the fourth row"
        machine.run(3, "RIGHT")
        machine.run(20)                        # its second choice, New Game+
        machine.run(3, "A")
        machine.run(100)                       # which game to carry on from
        machine.run(3, "A")                    # the first slot: its game loads, the menus close
        for _ in range(60):                    # and open again on the Battle Mode question
            machine.run(10)
            if machine.b(MENU_SCREEN) == NAMING_SCREEN:
                break
        assert machine.b(MENU_SCREEN) == NAMING_SCREEN, "the Battle Mode question opens"
        machine.run(40)                        # faded in
        machine.run(3, "A")
        machine.run(60)                        # Active (the default) accepted; Crono's naming screen
        for _ in range(40):                    # the naming screen wakes for a button before the cursor
            if run.map_id() not in ("0000", "b101"):   # moves, then Start accepts the name offered
                break
            machine.run(3, "START")
            machine.run(30)
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("New Game+ started, map", run.map_id(), "frame", machine.frame)
        assert run.map_id() not in ("0000", "b101"), "left the load and naming screens for the world map"
        run.checkpoint("started")
    run.say("end frame", machine.frame)
    run.finish("a New Game+ start")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
