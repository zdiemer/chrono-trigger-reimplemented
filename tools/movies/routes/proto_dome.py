"""Repair Robo at Proto Dome and return to the field with him in the party."""
from pathlib import Path

from ..campaign import REPO, Run, route_arguments, run_route
from .. import policies

SAVE = "oracle/saves/fantasyanime/ctsave03-future_more.srm"
PHASES = ["race", "world", "dome", "first_bugs", "second_bugs", "naming", "repaired"]
HEADER = """\
# Robo repaired at Proto Dome, 2300 A.D.; keep his name, choose Lucca to accompany him, and return to the field
# with Robo in the party (route proto_dome). The approach reuses events-jetbike-race-won's winning inputs.
# Save: ctsave03 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def bugs(run, seed):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    assert run.dialogs_until(lambda: run.machine.b(0x7E95D5) < 0x80, 6000), "bug battle begins"
    run.machine.run(180)
    run.fight("party", won, seed=seed, max_frames=16000)
    assert run.wait_control(6000), "the bugs beaten, the field back in hand"


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.play_movie(str(Path(REPO) / "oracle/movies/saves/events-jetbike-race-won.ctm"), 8083)
            assert m.b(0x7F0000) == 0x37, "before Robo's repair"
            assert m.b(0x7F0148) == 0, "repair not started"
            assert 3 not in m.peek(0x7E2980, 3)
            assert run.wait_control()
            run.checkpoint("race")
        if start <= 1:
            if run.map_id() != "f201":
                assert run.exit_to("f201"), "out of Lab 32"
            m.run(120)
            assert run.world_go(114 * 8, 51 * 8, max_nodes=4500), "Proto Dome on the world map"
            run.checkpoint("world")
        if start <= 2:
            assert run.world_exit(), "enter Proto Dome"
            assert run.map_id() == "e200", "Proto Dome"
            assert run.wait_control()
            run.checkpoint("dome")
        if start <= 3:
            assert run.walk(lambda m, x, y: x < 59 * 16 and y < 45 * 16, max_nodes=1600) is not None
            bugs(run, args.seed)
            assert m.b(0x7F0150) & 4
            run.checkpoint("first_bugs")
        if start <= 4:
            assert run.walk(lambda m, x, y: m.peek(0x7E0100, 2).hex() == "e200"
                            and m.b(0x7E011F) == 0 and y < 38 * 16, max_nodes=2400) is not None
            bugs(run, args.seed)
            assert m.b(0x7F0150) & 8
            run.checkpoint("second_bugs")
        if start <= 5:
            # Go around the lower platform on its right, then examine Robo.
            for frames, direction in ((24, "DOWN"), (104, "RIGHT"), (184, "UP"),
                                      (87, "LEFT"), (40, "UP")):
                m.run(frames, direction)
            m.run(8)
            m.run(3, "A")
            m.run(100)
            assert run.dialogs_until(lambda: m.b(0x7E0067) == 13, 20000), "Robo's naming screen"
            m.run(120)
            run.checkpoint("naming")
        if start <= 6:
            m.run(3, "START")
            m.run(180)
            assert run.dialogs_until(lambda: m.b(0x7F0000) == 0x3C, 16000), "Robo repaired"
            assert run.wait_control(6000)
            assert run.map_id() == "e200" and 3 in m.peek(0x7E2980, 3)
            run.checkpoint("repaired")
        run.finish("Robo repaired and the Proto Dome field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
