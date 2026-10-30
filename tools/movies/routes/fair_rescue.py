"""Win Bekkler's 80-point rescue game, receive the cat and return to the fair."""
from ..campaign import Run, route_arguments, run_route
from . import _fair as fair

SAVE = fair.SAVE
PHASES = ["square", "tent", "playing", "back"]
HEADER = fair.header("rescue", "Bekkler's 80-point rescue game: push back all three monsters, use the candy to raise the companion, win the cat, and return to the fair")


def move_across(run, x, tolerance):
    for _ in range(100):
        if abs(run.me()[0] - x) < tolerance:
            return
        run.machine.run(1, ("LEFT" if run.me()[0] > x else "RIGHT") + "+B")
    raise AssertionError("reach a launcher or the candy")


def kick(run):
    m = run.machine
    m.run(1, "UP")
    m.run(1)
    m.run(1, "A")
    m.run(1)


def play(run):
    m = run.machine
    run.player = m.b(0x7E0197) // 2
    # The rock launchers are at x=104,136,168, y=146. Walk up to their
    # aisle, then approach within talking reach without crossing each lane.
    for _ in range(30):
        if run.me()[1] <= 161:
            break
        m.run(1, "UP+B")
    served = False
    candies = 0
    for _ in range(300):
        assert run.map_id() == fair.TENT, "the rescue is still running"
        if fair.variable(m, 0x19) == 7:
            break
        assert m.b(fair.CONTROL), "the monsters have not caught the leader"
        # Local $12 is the companion's fall distance. At 178 the candy
        # appears; at 288 the companion falls. Candy calls the companion's
        # function 8 to raise them 112 pixels and subtract that distance.
        fall = m.w(0x7F0224)
        if fall < 178:
            served = False
        if fall >= 190 and not served:
            move_across(run, 48, 2)
            m.run(1, "UP")
            m.run(1)
            m.run(3, "A")
            m.run(1)
            served = True
            candies += 1
        for target in (0, 1, 2, 1):
            if fair.variable(m, 0x19) & (1 << target):
                continue
            move_across(run, 104 + 32 * target, 11)
            kick(run)
    assert fair.variable(m, 0x19) == 7, "all three monsters released"
    assert run.dialogs_until(lambda: run.map_id() == fair.SQUARE, 6000), "back outside"
    assert run.wait_control(), "the fair back in hand"
    assert m.b(0x7F005D) & 0x80, "the companion saved and the cat awarded"
    run.say("rescue won; candy used", candies, "times; cat flag", hex(m.b(0x7F005D)))


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
            fair.choose(run, 2)
            assert run.machine.b(fair.POINTS) == before - 80, "80 silver points paid"
            assert run.dialogs_until(lambda: run.machine.b(fair.CONTROL) == 1
                                     and fair.variable(run.machine, 0x0B) == 1, 6000), "the rescue begins"
            run.checkpoint("playing")
        if start <= 3:
            play(run)
            run.checkpoint("back")
        assert run.map_id() == fair.SQUARE and run.wait_control(), "the field in hand"
        run.finish("Bekkler's rescue game won")
        if args.movie:
            run.write_movie(args.movie, HEADER)
    finally:
        if run.machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
