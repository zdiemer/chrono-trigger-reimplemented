"""Hand-run Son of Sun and plant the Moon Stone, then save at the End of Time."""
from pathlib import Path

from .. import addresses as A, bfs
from ..campaign import Run, route_arguments, run_route
from . import son_of_sun as battle, sun_stone as stone
from .epoch_lavos import wait_until
from .save_point import press, to_slot, MENU_SCREEN, SAVE_POINT_SCREEN

SAVE = stone.SAVE
PHASES = stone.PHASES + ["return_epoch", "end_of_time", "saved"]
HEADER = """\
# Hand-run Sun Stone preparation: win Son of Sun, plant the Moon Stone in prehistory,
# then write slot 1 at the End of Time's save point (route sun_stone_save).
# Save: ctsave13 slot 3 (oracle/saves/fantasyanime).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def board(run):
    m = run.machine
    m.mark(4079)
    press(m, "A", 240)
    if m.b(A.OVERWORLD_TRAVEL) == 0:
        m.restore(4079)
        moves, _ = bfs.ow_bfs(m, lambda m, x, y: m.b(A.OVERWORLD_TRAVEL) != 0,
                             battle.world_pos, battle.world_map, confirm=True, max_nodes=250)
        assert moves is not None, "walk back to the Epoch"
    m.free(4079)
    assert wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 2, 1000)
    m.run(200)


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    start = run.start_at(PHASES)
    if start < len(stone.PHASES):
        stone.play(run, args.seed)
    if start <= 13:
        assert run.exit_to("f301")
        m.run(240)
        board(run)
        run.checkpoint("return_epoch")
    if start <= 14:
        stone.travel(run, 0, "d901")
        assert run.dialogs_until(run.dialog, 2000)
        press(m, "DOWN")
        press(m, "A")
        assert wait_until(m, lambda: run.map_id() == "d001", 3000)
        assert run.wait_control()
        run.checkpoint("end_of_time")
    if start <= 15:
        assert run.find_player()
        # The sprite's anchor is below the trigger tile (24, 22).
        assert run.go(392, 360, tolerance=4), "stand on the End of Time save point"
        # Face away from the nearby party members so confirm reaches the point.
        m.run(8, "LEFT")
        m.run(30)
        press(m, "A", 90)
        assert m.b(MENU_SCREEN) == SAVE_POINT_SCREEN, "the save point's screen opens"
        to_slot(m, 0)
        before = m.peek(0x306000, 8192)
        press(m, "A", 60)
        press(m, "A", 90)
        assert m.peek(0x306000, 8192) != before, "the game's save writer changed SRAM"
        press(m, "B", 60)
        assert run.wait_control()
        run.checkpoint("saved")
    assert run.map_id() == "d001" and m.b(stone.QUEST) == 7
    target = Path(run.prefix + ".srm")
    target.write_bytes(m.peek(0x306000, 8192))
    run.say("cartridge save", target)
    run.finish("the planted Moon Stone saved in slot 1 at the End of Time")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
