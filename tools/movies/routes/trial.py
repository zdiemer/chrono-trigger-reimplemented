"""Crono's arrest, the courtroom verdict and imprisonment from a normal save."""
from ..campaign import Run, route_arguments, run_route
from . import _trial_party as early
from ._trial_walk import world_enter

SAVE = "oracle/saves/routes/cronos-trial.srm"
TRIAL_PHASES = ["castle1000", "courtroom", "prison", "cell"]
PHASES = ["loaded"] + TRIAL_PHASES
HEADER = """# Crono brings Marle home: the arrest, trial and jury's verdict, then the prison sentence.
# Ends with Crono in his cell and the field in hand.
# Save: cronos-trial slot 1 (oracle/saves/routes/cronos-trial.srm), saved by the game outside Guardia Castle.
# Route: trial
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def play(run, start=0):
    if start <= 0:
        world_enter(run, 0x15)
        run.checkpoint("castle1000")
    if start <= 1:
        assert run.dialogs_until(lambda: run.map_id() == "1b00", 16000), "the courtroom"
        run.checkpoint("courtroom")
    if start <= 2:
        # Confirm chooses the first answer to each question. The testimony
        # revisits the fair, so wait for the prison rather than a map change.
        assert run.dialogs_until(lambda: run.map_id() == "4700", 30000), "the prison cell"
        run.checkpoint("prison")
    if start <= 3:
        assert run.wait_control(12000), "Crono awake in his cell"
        assert run.map_id() == "4700" and run.machine.b(early.STORY) == 0x2D
        assert run.machine.b(0x7F0059) == 0x10, "the verdict and sentence completed"
        run.say("jury count", run.machine.b(0x7F0051))
        run.checkpoint("cell")


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, 0, "f001", 0x27)
            run.checkpoint("loaded")
        play(run, max(0, start - 1))
        run.finish("the jury's verdict, the prison sentence and Crono in control in his cell")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
