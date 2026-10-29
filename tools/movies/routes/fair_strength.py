"""Ring the Millennial Fair's strength-test bell and collect a silver point."""
from ..campaign import Run, route_arguments, run_route
from . import _fair as fair

SAVE = fair.SAVE
PHASES = ["square", "ready", "back"]
HEADER = fair.header("strength", "The fair's strength test: start the swing, time the second A press, ring the bell, and collect one silver point")


def play(run):
    m = run.machine
    before = m.b(fair.POINTS)
    m.run(2, "UP")
    m.run(8)
    m.run(3, "A")
    # Object $1A calls the leader's function 3, which moves back and forth,
    # counting the distance in local $14 until A is held. A distance of at
    # least $15 rings the bell. Search the timing on a machine copy first.
    m.mark(4080)
    found = None
    for delay in range(600):
        if fair.variable(m, 0x14) >= 21 and m.b(fair.CONTROL) == 0:
            found = delay
            break
        m.probe(1)
    m.restore(4080)
    m.free(4080)
    assert found is not None, "the swing reaches the bell's strength"
    m.run(found)
    m.run(3, "A")
    assert run.wait_control(), "the strength test finished"
    assert m.b(fair.POINTS) == before + 1, "one silver point won"
    run.say("bell rung; silver points", before, "->", m.b(fair.POINTS))


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
            for point in ((392, 640), (392, 560), (312, 528), (232, 528), (152, 528), (88, 586)):
                fair.nearby(run, *point)
            assert run.go(89, 586, tolerance=3), "below the strength tester"
            run.checkpoint("ready")
        if start <= 2:
            play(run)
            run.checkpoint("back")
        assert run.map_id() == fair.SQUARE and run.wait_control(), "the fair back in hand"
        run.finish("the strength-test bell rung")
        if args.movie:
            run.write_movie(args.movie, HEADER)
    finally:
        if run.machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
