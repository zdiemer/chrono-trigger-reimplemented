"""Fly the winged Epoch over the Black Omen and confirm: the party goes into the Omen.

Save: ctsave14 slot 1 (fantasyanime archive; the party on the 1000 A.D. world map beside the
winged Epoch, the Black Omen risen).

Board the Epoch, fly over the Omen, whose name the sign then shows, and confirm: the map leaves
for the Omen ($C2:49CF, the scene EPOCH_ENTERS_THE_BLACK_OMEN), the Epoch hovering for the frames
that takes, with the location already the Omen's ($C2:42DD), until the Omen's field is in hand.
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route
from ._epoch import board, fly_to

HEADER = """\
# The winged Epoch into the Black Omen: board it on the 1000 A.D. map, fly over the Omen and confirm,
# until the Omen's field is in hand (route black_omen_entry).
# Save: ctsave14 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "saves/fantasyanime/ctsave14-side_quests3.srm"
SLOT = 0
PHASES = ["loaded", "boarded", "over_the_omen", "inside"]
WORLD_1000 = "f001"
BLACK_OMEN = 0x7E02A2           # where the Omen stands on the map: x, then y (overworld_black_omen)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(SLOT)
            machine.run(4, "A")
            for _ in range(200):
                machine.run(10)
                if run.map_id() not in ("0000", "b101"):
                    break
            machine.run(120)
            assert run.map_id() == WORLD_1000, "the 1000 A.D. world map"
            run.checkpoint("loaded")
        if start <= 1:
            board(run)
            run.checkpoint("boarded")
        if start <= 2:
            fly_to(run, machine.w(BLACK_OMEN), machine.w(BLACK_OMEN + 2))
            machine.run(20)
            run.checkpoint("over_the_omen")
        if start <= 3:
            machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() != WORLD_1000 and machine.b(A.FIELD_PLAYER_CONTROL),
                                     8000), "into the Black Omen"
            assert run.wait_control(), "the Omen's field in hand"
            run.say("inside", run.map_id(), "at", run.me())
            run.checkpoint("inside")
        run.finish("the Epoch flown into the Black Omen")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
