"""Hand-run route from the Cathedral through Crono's trial and imprisonment."""
from pathlib import Path

from ..campaign import Run, route_arguments, run_route
from . import _trial_party as early
from . import _trial_cathedral as cathedral
from . import trial
from ._trial_walk import toward, field_exit, world_enter

SAVE = cathedral.SAVE
HEADER = """# The Cathedral, Leene restored, home to 1000 A.D., Crono's arrest and trial.
# Save: ctsave01 slot 2 (oracle/saves/fantasyanime/ctsave01-kidnapping.srm).
# Route: cronos_trial (hand-run; keep this long movie outside the library).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["leene", "castle", "stairs", "marle", "reunited", "castle_return",
          "castle_out", "forest600", "forest_out", "canyon", "gate600", "gate_ready", "home_world",
          "forest1000", "before_castle", "saved"] + trial.TRIAL_PHASES


def spiral(run, direction, destination):
    """The three flights share a location number and join by held directions."""
    assert run.wait_control()
    for _ in range(250):
        if run.map_id() == destination:
            return
        run.machine.run(8, direction)
    raise AssertionError("the spiral staircase's destination")


def save_before_trial(run):
    """Save through the overworld menu, then export the cartridge's save RAM."""
    from .menu_sweep import open_menu, press, CURSOR, MENU_SCREEN, back_to_main_menu
    from .save_point import to_slot
    machine = run.machine
    toward(run, 280, 208, lambda: run.world_pos() == (280, 208), step=8, world=True)
    before = machine.peek(0x306000, 8192)
    open_menu(machine)
    for _ in range(8):
        if machine.b(CURSOR) == 5:
            break
        press(machine, "RIGHT")
    assert machine.b(CURSOR) == 5
    press(machine, "A", 60)
    assert machine.b(MENU_SCREEN) == 6, "the normal save screen"
    to_slot(machine, 0)
    press(machine, "A", 60)
    press(machine, "A", 120)
    back_to_main_menu(machine)
    press(machine, "B", 120)
    saved = machine.peek(0x306000, 8192)
    assert saved != before, "the game wrote the save"
    path = Path(run.prefix + "-before-trial.srm")
    path.write_bytes(saved)
    run.say("cartridge save", path)


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start == 0:
            cathedral.cathedral(run, args.seed)
            assert early.advance(run, "DOWN", lambda: run.me()[1] >= 350)
            run.machine.run(8)
            run.machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() == "7800", 12000)
            run.checkpoint("leene")
        if start <= 1:
            assert run.wait_control(12000), "Leene restored at Guardia"
            assert run.machine.b(early.STORY) == 0x1C
            run.checkpoint("castle")
        if start <= 2:
            toward(run, 616, 136, lambda: run.map_id() == "d401")
            run.checkpoint("stairs")
        if start <= 3:
            spiral(run, "RIGHT", "7a00")
            run.checkpoint("marle")
        if start <= 4:
            assert run.wait_control()
            toward(run, 648, 696, lambda: not run.machine.b(early.PLAYER_CONTROL))
            assert run.wait_control(20000), "Marle restored"
            assert run.machine.b(early.STORY) == 0x21
            run.checkpoint("reunited")
        if start <= 5:
            toward(run, 872, 728, lambda: run.map_id() == "d401")
            spiral(run, "LEFT", "7800")
            assert run.wait_control()
            run.checkpoint("castle_return")
        if start <= 6:
            field_exit(run, 504, 888, "f101")
            run.machine.run(120)
            run.checkpoint("castle_out")
        if start <= 7:
            world_enter(run, 0x77, row=15)
            run.checkpoint("forest600")
        if start <= 8:
            assert run.wait_control()
            for _ in range(8):
                toward(run, 296, 760,
                       lambda: (run.map_id() == "f101" and run.me()[1] > 700)
                       or (run.me()[1] > 100 and not run.machine.b(early.PLAYER_CONTROL)))
                if run.map_id() == "f101":
                    break
                early.settle(run, args.seed)
            assert run.map_id() == "f101"
            run.machine.run(120)
            run.checkpoint("forest_out")
        if start <= 9:
            world_enter(run, 0x70)
            assert run.wait_control()
            run.checkpoint("canyon")
        if start <= 10:
            for _ in range(8):
                toward(run, 504, 200,
                       lambda: run.map_id() == "7100"
                       or (run.me()[1] < 430 and not run.machine.b(early.PLAYER_CONTROL)))
                if run.map_id() == "7100":
                    break
                early.settle(run, args.seed)
            assert run.map_id() == "7100"
            run.checkpoint("gate600")
        if start <= 11:
            assert run.wait_control(20000)
            assert run.map_id() == "0800" and run.machine.b(early.STORY) == 0x27
            run.checkpoint("gate_ready")
        if start <= 12:
            field_exit(run, 120, 232, "b701")
            field_exit(run, 392, 360, "0500")
            field_exit(run, 392, 760, "f001")
            run.machine.run(120)
            run.checkpoint("home_world")
        if start <= 13:
            world_enter(run, 0x13, row=17)
            run.checkpoint("forest1000")
        if start <= 14:
            assert run.wait_control()
            for _ in range(8):
                toward(run, 296, 24,
                       lambda: (run.map_id() == "f001" and run.me()[1] < 100)
                       or (run.me()[1] < 700 and not run.machine.b(early.PLAYER_CONTROL)))
                if run.map_id() == "f001":
                    break
                early.settle(run, args.seed)
            assert run.map_id() == "f001"
            run.machine.run(120)
            run.checkpoint("before_castle")
        if start <= 15:
            save_before_trial(run)
            run.checkpoint("saved")
        trial.play(run, max(0, start - 16))
        run.finish("Leene restored, home to 1000 A.D., the trial and Crono in control in his cell")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
