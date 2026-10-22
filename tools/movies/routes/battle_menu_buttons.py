"""The battle menu's other buttons: at Spekkio's at the End of Time, the first menu of the fight
is worked with the buttons the battle bot never presses, and the movie stops there.

Save: zophar chrono slot 1, as tech_sweep_spekkio (all seven at the End of Time, $1D0). The bot
(tools/movies/bot.py) moves the command cursor up and down, opens a list and takes an entry; the
menu also goes to another member waiting with right and left, turns the item list a page with L
and R, leaves a list with B, and attacks at once with X where the commands are remembered
(game/battle/menu_input.c). Folded into the tech sweep's last fight these lengthened it by some
26000 frames (the fight's choices go another way), so they are a movie of their own, which stops
in the fight (docs/movies-wanted.md, "Every tech").
"""
from ..campaign import Run, route_arguments, run_route
from . import tech_sweep_spekkio as sweep

HEADER = """\
# The battle menu's other buttons: load the save (all seven at the End of Time), walk into Spekkio's room and start his
# fight, then at the first menu go right and left to the other members waiting, page the item list with L and R and
# leave it with B, move across the tech list and leave it, and attack with X; the movie stops in the fight (route
# battle_menu_buttons).
# Save: zophar chrono slot 1 (oracle/saves/zophar, zophar.net Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = sweep.SAVE
SLOT = sweep.SLOT
PHASES = ["loaded", "fight", "swept"]

# The buttons, from the command page of the member whose menu opens: right twice and left (other
# members waiting), down to Item and into it, L, R, R, L (a page each way twice), down, B (back),
# up to Tech and into it, right and left (across the list), B, up to Attack, X.
BUTTONS = ["RIGHT", "RIGHT", "LEFT", "DOWN", "DOWN", "A", "L", "R", "R", "L", "DOWN", "B", "UP", "A", "RIGHT", "LEFT",
           "B", "UP", "X"]
MENU_MEMBER, MENU_PAGE, TARGETING = 0x7E95D5, 0x7E95DB, 0x7E9609
MEMBERS_WAIT = 600              # frames for the other members' turns to come first
AFTER = 300                     # frames the movie runs on after the last button


def menu_open(machine):
    return machine.b(MENU_MEMBER) < 3 and machine.b(MENU_PAGE) == 0 and machine.b(TARGETING) == 0


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
        assert run.wait_control(), "the End of Time in hand"
        assert run.map_id() == sweep.END_OF_TIME, "the End of Time"
        run.checkpoint("loaded")
    if start <= 1:
        sweep.enter_spekkio(run)
        assert run.go(*sweep.SPEKKIO_SPOT, tolerance=3), "beside Spekkio"
        machine.run(2, "DOWN")
        machine.run(8)
        for attempt in range(40):
            if machine.w(sweep.ENEMY_HP) == 10000 and machine.b(MENU_MEMBER) < 0x80:
                break
            if run.dialog() or attempt < 2:
                machine.run(3, "A")
            machine.run(40)
        assert machine.w(sweep.ENEMY_HP) == 10000, "Spekkio's fight starts"
        run.checkpoint("fight")
    if start <= 2:
        machine.run(MEMBERS_WAIT)
        for _ in range(1200):
            if menu_open(machine):
                break
            machine.run(1)
        assert menu_open(machine), "a member's menu open"
        pages = set()
        for button in BUTTONS:
            machine.run(3, button)
            machine.run(20)
            pages.add(machine.b(MENU_PAGE))
        run.say("pages seen", sorted(pages), "frame", machine.frame)
        assert {0, 1, 2} <= pages, "the command page and both lists"
        machine.run(AFTER)
        run.checkpoint("swept")
    run.finish("the battle menu's other buttons")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
