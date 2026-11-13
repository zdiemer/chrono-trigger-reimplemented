"""Prepare a cartridge save at the cave save point before Heckran.

ctsave03 slot 3 has story $4D; the cave's boss exists below $51. ctsave04
slot 1 is already $54. Take the Medina gate and walk to Heckran's Cave.
"""
from pathlib import Path

from .heckran import fight
from .save_point import press, to_slot, MENU_SCREEN, SAVE_POINT_SCREEN
from ..campaign import Run, route_arguments, run_route

SAVE = "saves/fantasyanime/ctsave03-future_more.srm"
PHASES = ["loaded", "heckran_medina", "heckran_world", "heckran_entrance", "heckran_inner", "heckran_passage",
          "heckran_return", "heckran_south", "heckran_bend", "heckran_water", "heckran_saved"]
HEADER = """\
# Heckran's Cave, 1000 A.D.: from the End of Time through the Medina gate and the cave,
# save normally at the cave save point before Heckran (hand-run route heckran_prepare).
# Save: ctsave03 slot 3 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def cave_walk(run, goal, seed):
    """Walk to a waypoint, resolving the cave's one-time encounters on the way."""
    m = run.machine
    for _ in range(10):
        assert run.find_player() is not None
        run.say("cave walk", run.map_id(), run.me(), "frame", m.frame)
        flags = m.b(0x7F01AE)
        moves = run.walk(lambda machine, x, y: goal(machine, x, y)
                         or machine.b(0x7F01AE) != flags or run.dialog())
        if moves is None:
            m.save_state(run.prefix + "_walk.st")
            m.screenshot(run.prefix + "_walk.png")
            raise AssertionError("cave waypoint")
        # A trigger may set its one-time flag on the goal tile. Finish that
        # encounter before declaring the waypoint reached.
        if goal(m, *run.me()) and m.b(0x7F01AE) == flags:
            m.run(60)
            return
        assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 6000), "cave encounter"
        fight(run, seed)
    raise AssertionError("too many cave encounters")


def cave_exit(run, want, seed):
    cave_walk(run, lambda m, x, y: run.map_id() == want, seed)


def play(run, start=1, seed=1, save=True):
    """From the End of Time (story $4D) through the Medina gate and Heckran's Cave
    to its save point; `start` is the index in PHASES of the first phase to run.
    `save` false stops by the save point without saving (a playthrough goes on)."""
    m = run.machine
    if start <= 1:
        assert run.walk(lambda machine, x, y: machine.b(0x7F020E) == 1) is not None, "open the gate platform's door"
        assert run.wait_control()
        assert run.walk(lambda machine, x, y: machine.b(0x7F0212) == 14
                        and machine.b(0x7F0214) == 8) is not None, "the Medina gate"
        m.run(3, "A")
        m.run(60)
        m.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == "2400", 8000), "Medina"
        assert run.wait_control()
        run.checkpoint("heckran_medina")
    if start <= 2:
        if not m.b(0x7F01A2) & 1:
            assert run.walk(lambda machine, x, y: machine.b(0x7F01A2) & 1) is not None, "Medina's welcome"
            assert run.wait_control(), "the villagers' conversation"
        assert run.exit_to("f001"), "the 1000 A.D. map"
        m.run(120)
        run.checkpoint("heckran_world")
    if start <= 3:
        from .. import bfs
        moves, _ = bfs.ow_bfs(m, lambda machine, x, y: run.map_id() == "3000",
                              lambda machine: run.world_pos(), lambda machine: run.map_id(),
                              confirm=True)
        assert moves is not None, "Heckran's Cave"
        run.checkpoint("heckran_entrance")
    if start <= 4:
        assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 6000), "entrance encounter"
        fight(run, seed)
        cave_exit(run, "3100", seed)
        assert run.wait_control()
        run.checkpoint("heckran_inner")
    if start <= 5:
        cave_exit(run, "2f00", seed)
        assert run.wait_control()
        run.say("passage at", run.me())
        run.checkpoint("heckran_passage")
    if start <= 6:
        cave_walk(run, lambda machine, x, y: bool(machine.b(0x7F01AE) & 0x40), seed)
        cave_exit(run, "3100", seed)
        assert run.wait_control()
        run.checkpoint("heckran_return")
    if start <= 7:
        cave_walk(run, lambda machine, x, y: x < 18 * 16 and y > 59 * 16, seed)
        cave_exit(run, "2f00", seed)
        assert run.wait_control()
        run.checkpoint("heckran_south")
    if start <= 8:
        cave_walk(run, lambda machine, x, y: x < 20 * 16, seed)
        run.checkpoint("heckran_bend")
    if start <= 9:
        cave_exit(run, "3100", seed)
        assert run.wait_control()
        run.checkpoint("heckran_water")
    if start <= 10 and save:
        assert run.walk(lambda machine, x, y: machine.b(0x7F01CF) & 0x80) is not None, "the cave save point"
        run.say("save point", run.me(), "frame", m.frame)
        m.run(60)
        m.save_state(run.prefix + "_point.st")
        # This map's object loop can miss a three-frame confirm pulse.
        press(m, "A", 90, hold=30)
        run.say("save screen", m.b(MENU_SCREEN))
        assert m.b(MENU_SCREEN) == SAVE_POINT_SCREEN, "save screen opened"
        to_slot(m, 2)
        press(m, "A", 60)
        press(m, "A", 90)
        press(m, "B", 30)
        assert run.wait_control()
        assert m.b(0x7F0000) < 0x51, "Heckran still alive"
        run.checkpoint("heckran_saved")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(2)
            m.run(4, "A")
            m.run(300)
            assert run.wait_control()
            assert run.map_id() == "d001" and m.b(0x7F0000) == 0x4D
            run.checkpoint("loaded")
        play(run, start, args.seed)
        # This is cartridge SRAM written through the game's own save menu.
        # Keep the machine checkpoints and the long preparation movie in scratch.
        Path(run.prefix + ".srm").write_bytes(m.peek(0x306000, 8192))
        run.finish("the cave save point before Heckran")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if m.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
