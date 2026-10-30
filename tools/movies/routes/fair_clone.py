"""Win Bekkler's 40-point clone game and return to the lower square."""
from ..campaign import Run, route_arguments, run_route
from . import _fair as fair

SAVE = fair.SAVE
PHASES = ["square", "tent", "paid", "back"]
HEADER = fair.header("clone", "Bekkler's clone game: pay 40 silver points, copy all 22 poses, receive the clone, and return to the fair")


def play(run):
    m = run.machine
    assert not m.b(0x7F005E) & 1, "Crono's clone not yet awarded in this save"
    # Local $08 counts poses; $11=$80 opens an answer window, and $10
    # names the pose. Object $0A reads R, L, A, Y into $11 in that order.
    answered = set()
    start = m.frame
    while run.map_id() == fair.TENT and m.frame - start < 12000:
        pose_number, answer = fair.variable(m, 8), fair.variable(m, 0x11)
        if pose_number and answer == 0x80:
            pose = fair.variable(m, 0x10)
            assert pose < 4, "one of the clone's four poses"
            answered.add(pose_number)
            m.run(2, ("R", "L", "A", "Y")[pose])
        elif run.dialog():
            m.run(3, "A")
            m.run(37)
        else:
            m.run(2)
    assert answered == set(range(1, 23)), "all 22 poses copied"
    assert run.map_id() == fair.SQUARE and run.wait_control(), "the fair back in hand"
    assert m.b(0x7F005E) & 1, "Crono's clone awarded"
    run.say("copied", len(answered), "poses; silver points", m.b(fair.POINTS))


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
            fair.choose(run, 1)
            assert run.machine.b(fair.POINTS) == before - 40, "40 silver points paid"
            run.checkpoint("paid")
        if start <= 3:
            play(run)
            run.checkpoint("back")
        assert run.map_id() == fair.SQUARE and run.wait_control(), "the field in hand"
        run.finish("Bekkler's clone game won")
        if args.movie:
            run.write_movie(args.movie, HEADER)
    finally:
        # A --until checkpoint closes the run before raising RouteDone.
        if run.machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
