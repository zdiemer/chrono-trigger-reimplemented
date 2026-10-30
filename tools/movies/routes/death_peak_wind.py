"""Climb Death Peak's wind slope, using both trees as shelter.

ctsave11 slot 3 has the Chrono Trigger, clone and Keeper's instructions.
The guide at the first shelter becomes a tree when spoken to. Stay still
there; keep pressing up at the second tree, where idling lets the slope
carry the party out of shelter. Search crossings on machine copies.
"""
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave11-crono_gone.srm"
PHASES = ["loaded", "entered", "tree", "second", "after"]
WIND = 0x7F0218
GUIDES = 0x7F007C
SLOPE, LOWER = "f400", "f500"
HEADER = """\
# Death Peak, 2300 A.D.: enter the wind slope, reach the first shelter and speak to the guide,
# turn it into a tree, cross to the second tree and climb to the next field (route death_peak_wind).
# Crossings are searched on machine copies; only successful inputs are kept.
# Save: ctsave11 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def fresh_calm(run, hold=0):
    """Stay against the trunk through a gust, then leave at the next calm."""
    m = run.machine
    for want in (1, 0):
        for _ in range(240):
            if m.b(WIND) == want:
                break
            m.run(4, hold)
        assert m.b(WIND) == want and run.map_id() == SLOPE, "sheltered through the gust"
    run.say("calm", m.frame, "player", run.player, "position", run.me())


def crossing(run, side_frames, goal, limit):
    """Try small departure delays, restoring every trial before its replay."""
    m = run.machine
    m.mark(4000)
    winner = None
    for delay in range(0, 41, 4):
        m.restore(4000)
        m.probe(delay, "UP+B")
        m.probe(side_frames, "RIGHT+B")
        for elapsed in range(4, limit + 1, 4):
            m.probe(4, "UP+B")
            if goal():
                winner = delay, elapsed
                break
            if run.map_id() != SLOPE:
                break
        if winner is not None:
            break
        run.say("crossing trial", delay, "ended", run.map_id(), run.me())
    m.restore(4000)
    m.free(4000)
    assert winner is not None, "a safe crossing found on a machine copy"
    delay, elapsed = winner
    m.run(delay, "UP+B")
    m.run(side_frames, "RIGHT+B")
    m.run(elapsed, "UP+B")
    assert goal(), "the winning crossing replayed"
    run.say("crossing", "delay", delay, "side", side_frames, "up", elapsed, "frame", m.frame)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(2)
            m.run(4, "A")
            m.run(500)
            assert run.map_id() == "f201", "outside Death Peak"
            assert m.b(GUIDES) & 3 == 3, "Keeper's instructions completed"
            items = m.peek(0x7E2400, 242)
            assert 0xD9 in items and 0xE2 in items, "Chrono Trigger and clone"
            run.checkpoint("loaded")
        if start <= 1:
            assert run.world_exit(max_nodes=100) and run.map_id() == SLOPE
            assert run.find_player() is not None
            run.checkpoint("entered")
        else:
            assert run.find_player() is not None
        if start <= 2:
            crossing(run, 0, lambda: run.me()[1] <= 806, 240)
            m.run(2, "UP+B")
            m.run(3, "A")
            m.run(90)
            run.close_dialogs()
            assert run.dialogs_until(lambda: bool(m.b(GUIDES) & 4), 2000), "the guide becomes a tree"
            assert run.wait_control(), "control after the tree's transformation"
            run.checkpoint("tree")
        if start <= 3:
            fresh_calm(run)
            crossing(run, 12, lambda: run.map_id() == SLOPE and run.me()[1] <= 521, 360)
            run.checkpoint("second")
        if start <= 4:
            fresh_calm(run, "UP+B")
            crossing(run, 12, lambda: run.map_id() == LOWER, 400)
            assert run.wait_control(), "the field beyond the wind in hand"
            run.checkpoint("after")
        assert run.map_id() == LOWER
        run.finish("Death Peak's wind slope climbed")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        # --until closes its machine before raising RouteDone.
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
