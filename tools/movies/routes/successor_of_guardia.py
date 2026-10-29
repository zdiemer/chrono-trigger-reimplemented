"""The Successor of Guardia, technoplaza ct-state17 slot 2."""
from ..campaign import run_route
from ..other_endings import telepod_ending

SAVE = "oracle/saves/technoplaza/ct-state17.srm"


def ending(run):
    # Location $052's dispatcher: story $27 <= progress < $2D sets $6E to 5
    # and returns to the telepods. The square's two guards then start $04F.
    if not run.resume("square"):
        assert run.dialogs_until(lambda: run.map_id() == "0800" and run.machine.b(0x7F006E) == 5,
                                 12000), "Successor selected by the game's ending dispatcher"
        assert run.wait_control(), "back at the telepods"
        assert run.exit_to("b701"), "bell plaza"
        assert run.wait_control()
        assert run.exit_to("0500"), "lower square"
        assert run.wait_control()
        run.checkpoint("square")
    # Around the fountain's east side and the visitor on the south stairs.
    # The guard's south side is beyond the exit; approach from the north.
    for x, y in ((392, 504), (432, 552), (432, 576), (406, 608), (406, 720), (383, 728)):
        assert run.go(x, y, tolerance=5), "the path to the royal guards"
    run.machine.run(2, "DOWN")
    run.machine.run(8)
    run.machine.run(3, "A")
    assert run.dialogs_until(lambda: run.map_id() == "4f00", 3000), "the royal family"
    run.checkpoint("guard")
    # $057 object 0 ends on its halt at offset $0270 after Frog and Leene,
    # the heart and the fade. Do not mistake an elapsed timeout for success.
    assert run.dialogs_until(lambda: run.map_id() == "5700" and run.machine.w(0x7E1180) == 0x0270,
                             20000), "Frog and Leene's final scene has finished"
    run.machine.run(120)
    assert run.map_id() == "5700" and run.machine.w(0x7E1180) == 0x0270


def main():
    telepod_ending(save=SAVE, title="The Successor of Guardia", ending=ending)


if __name__ == "__main__":
    raise SystemExit(run_route(main))
