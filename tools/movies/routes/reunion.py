"""Reunion, technoplaza ct-state26 slot 2, with Crono still absent."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state26.srm"
PHASES = ["reunion", "departure", "parade", "king"]


def ending(run):
    machine = run.machine
    variable = lambda index: machine.b(0x7F0200 + 2 * index)
    start = run.start_at(PHASES)

    if start <= 0:
        assert run.dialogs_until(lambda: run.map_id() == "5400", 20000), "the End of Time reunion"
        # $052: story >= $D4, no Crono and $6D bit 7 clear select $6E = 4.
        assert machine.b(0x7F006E) == 4, "Reunion without crashing the Epoch"
        run.checkpoint("reunion")
    if start <= 1:
        assert run.wait_control(), "Marle at the End of Time"
        # Gaspar wakes the companions; each conversation sends one to the Gate.
        assert run.talk(7, done=lambda: variable(0x0B) == 1, presses=20), "Gaspar"
        for obj, flag in ((3, 0x0E), (4, 0x0F), (5, 0x0D)):
            assert run.talk(obj, done=lambda flag=flag: variable(flag) == 1,
                            presses=20), "a companion joining the departure"
        # An absent Magus counts himself in at map setup; otherwise speak to him.
        if variable(0x15) < 4:
            assert run.talk(6, done=lambda: variable(0x10) == 1, presses=20), "Magus"
        assert run.dialogs_until(lambda: variable(0x15) == 4, 6000), "all companions ready"
        # Crossing column $13 starts the scene, potentially before a walk ends.
        assert run.walk(lambda m, x, y: x < 19 * 16
                        or m.peek(0x7E0100, 2).hex() != "5400") is not None
        assert run.dialogs_until(lambda: run.map_id() == "5000", 12000), "the Moonlight Parade"
        run.checkpoint("departure")
    if start <= 2:
        assert run.wait_control()
        assert run.exit_to("5100"), "Leene's Bell"
        assert run.wait_control()
        assert run.exit_to("5500"), "the telepod farewells"
        assert run.wait_control(max_frames=12000), "Marle after the farewells"
        assert run.exit_to("5100"), "back to the bell"
        run.checkpoint("parade")
    if start <= 3:
        assert run.wait_control()
        assert run.talk(9), "the King at Leene's Bell"
        run.checkpoint("king")

    modes = set()

    def selected():
        modes.add(machine.b(0x7E0384) & 0x1F)
        return machine.b(0x7F006E) == 4 and 11 in modes

    # $051 calls mode 11; the later $05A calls mode 15 for $6E = 4.
    # $056's credits then lead to $058's final halt, after The End's reveal.
    watch_ending(run, selected=selected,
                 done=lambda: run.map_id() == "5800" and 15 in modes
                 and machine.w(0x7E1180) == 0x0052,
                 max_frames=90000)
    run.say("ending modes observed", sorted(modes))


def main():
    play_ending(save=SAVE, title="Reunion", ending=ending,
                entry="telepod", party=3, story=0xD4, core_policy="party_lavos_core")


if __name__ == "__main__":
    raise SystemExit(run_route(main))
