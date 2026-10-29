"""Win Bekkler's 10-point soldier shuffle and return to the lower square."""
from ..campaign import Run, route_arguments, run_route
from . import _fair as fair

SAVE = fair.SAVE
PHASES = ["square", "tent", "paid", "back"]
HEADER = fair.header("shuffle", "Bekkler's soldier shuffle: pay 10 silver points, follow the named soldier through the shuffle, win the Poyozo Doll, and return to the fair")


def play(run):
    m = run.machine
    assert run.dialogs_until(lambda: fair.variable(m, 0x0B) == 1, 6000), "soldiers shuffled"
    assert run.wait_control(), "the player can identify a soldier"
    # Local $0C identifies the requested soldier. Objects $14-$16 retain
    # their identity while local $0E chooses the shuffle's permutation.
    target = fair.variable(m, 0x0C)
    assert target in (1, 2, 3), "one of the three soldiers named"
    before = m.b(0x7F005D)
    assert run.talk(19 + target), "the named soldier answers"
    assert run.dialogs_until(lambda: run.map_id() == fair.SQUARE, 6000), "back outside"
    assert run.wait_control(), "the fair back in hand"
    assert m.b(0x7F005D) == before | 1, "Crono's Poyozo Doll won"
    run.say("soldier", target, "identified; Poyozo Doll flag", hex(m.b(0x7F005D)))


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            fair.square(run)
            run.checkpoint("square")
        if start <= 1:
            fair.tent(run)
            run.checkpoint("tent")
        if start <= 2:
            before = run.machine.b(fair.POINTS)
            fair.greet(run)
            fair.choose(run, 0)
            assert run.machine.b(fair.POINTS) == before - 10, "10 silver points paid"
            run.checkpoint("paid")
        if start <= 3:
            play(run)
            run.checkpoint("back")
        assert run.map_id() == fair.SQUARE and run.wait_control(), "the field in hand"
        run.finish("Bekkler's soldier shuffle won")
        if args.movie:
            run.write_movie(args.movie, HEADER)
    finally:
        if run.machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
