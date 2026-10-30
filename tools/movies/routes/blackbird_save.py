"""Prepare a cartridge save for Dalton after the Blackbird movie's opening."""
from pathlib import Path

from ..campaign import REPO, Run, route_arguments, run_route
from .. import policies
from .save_point import press, to_slot
from .rainbow_shell_trial import fast_messages
from .menu_sweep import open_menu, open_option, use_item, back_to_main_menu, ITEMS, SHELTER

SAVE = "oracle/saves/fantasyanime/ctsave11-crono_gone.srm"
HEADER = """\
# Prepare the Blackbird cartridge save for Dalton (route blackbird_save; hand-run only).
# Save: ctsave11 slot 1 (oracle/saves/fantasyanime).
# Opening, equipment and inventory recovery, return to the cell, Shelter and normal save.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["opening", "cell", "ducts", "marle_room", "marle_gear", "robo_room", "robo_gear", "lucca_room", "lucca_gear", "inventory_room", "inventory", "cell_ready", "saved"]


def fight(run, seed=1):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    assert run.dialogs_until(lambda: run.machine.b(0x7E95D5) < 0x80, 6000)
    run.machine.run(180)
    run.fight("party", won, seed=seed, max_frames=24000)
    assert run.wait_control()


def travel(run, want):
    """Follow a field exit, winning any guard encounter on its approach."""
    m = run.machine
    for _ in range(8):
        if run.map_id() == want:
            m.run(120)
            run.wait_control()
            return True
        run.wait_control()
        here = run.map_id()
        moves = run.walk(lambda m, x, y: m.peek(0x7E0100, 2).hex() == want
                         or (m.peek(0x7E0100, 2).hex() == here and m.b(0x7E011F) == 0))
        if moves is None:
            return False
        m.run(120)
        run.say("travel", want, "now", run.map_id(), "frame", m.frame)
        if run.map_id() != want:
            fight(run)
    return False


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.play_movie(str(Path(REPO) / "oracle/movies/saves/events-blackbird.ctm"), 4400)
            assert m.b(0x7F0000) == 0xCF
            assert m.b(0x7F00D2) == 0 and not m.b(0x7F00BA) & 0x80
            # The narrow hull ledge blocks three directions, so find_player's
            # two-sided movement test does not recognize control here.
            assert m.b(0x7E011F) and run.map_id() == "6c01"
            run.checkpoint("opening")
        if start <= 1:
            for frames, direction in ((32, "DOWN"), (24, "LEFT"), (32, "DOWN")):
                m.run(frames, direction)
                m.run(30)
            assert run.map_id() == "7301", "return to the cell"
            assert run.wait_control()
            run.checkpoint("cell")
        if start <= 2:
            m.run(60, "DOWN")
            assert run.wait_control()
            assert m.b(0x7F00B3) & 0x40, "the party has noticed the air duct"
            assert run.talk(11, done=lambda: run.map_id() == "7801"), "enter the ducts"
            assert run.wait_control()
            run.checkpoint("ducts")
        if start <= 3:
            m.run(12, "DOWN")
            assert run.wait_control()
            assert run.exit_to("bc01"), "the sleeping guard's equipment room"
            assert run.wait_control()
            run.checkpoint("marle_room")
        if start <= 4:
            assert run.talk(6, done=lambda: m.b(0x7F00AF) & 1)
            assert run.wait_control()
            assert m.b(0x7F00D1) & 0x80
            run.checkpoint("marle_gear")
        if start <= 5:
            assert run.exit_to("7801")
            assert run.exit_to("7501")
            assert run.wait_control()
            run.checkpoint("robo_room")
        if start <= 6:
            assert run.walk(lambda m, x, y: m.b(0x7E011F) == 0 and y > 96) is not None
            fight(run)
            assert run.talk(9, done=lambda: m.b(0x7F00AF) & 4)
            assert run.wait_control()
            run.checkpoint("robo_gear")
        if start <= 7:
            for location in ("7001", "bb01"):
                assert travel(run, location)
            run.checkpoint("lucca_room")
        if start <= 8:
            # Climb the ladder into the Byte. Its collision stops the leader
            # before the battle begins, so a position-only search misses it.
            m.run(24, "RIGHT")
            m.run(10)
            m.run(100, "UP")
            m.run(10)
            fight(run)
            assert run.talk(6, done=lambda: m.b(0x7F00AF) & 2)
            assert run.wait_control()
            run.checkpoint("lucca_gear")
        if start <= 9:
            for location in ("7001", "7101", "7601"):
                assert travel(run, location)
            run.checkpoint("inventory_room")
        if start <= 10:
            assert run.walk(lambda m, x, y: m.b(0x7E011F) == 0 and y < 456) is not None
            fight(run)
            assert run.talk(9, done=lambda: m.b(0x7F00BA) & 2)
            assert run.wait_control()
            run.checkpoint("inventory")
        if start <= 11:
            assert travel(run, "7101")
            # This approach clears the upper-deck patrols, then stops at the
            # closed route to the lower deck. Return through the ducts.
            travel(run, "7001")
            assert run.map_id() == "7101"
            assert run.exit_to("7801"), "return through the upper deck's duct"
            assert run.exit_to("7301"), "the cell's save point"
            assert run.wait_control()
            run.checkpoint("cell_ready")
        if start <= 12:
            fast_messages(m)
            assert run.wait_control()
            assert run.go(21 * 16 + 8, 10 * 16 + 8)
            m.run(120)
            open_menu(m)
            open_option(m, ITEMS)
            use_item(run, SHELTER, choose_member=True)
            back_to_main_menu(m)
            press(m, "B", 120)
            assert run.wait_control()
            for member in m.peek(0x7E2980, 3):
                base = 0x7E2600 + member * 0x50
                assert m.w(base + 3) == m.w(base + 5) and m.w(base + 7) == m.w(base + 9), "rested at the save point"
            press(m, "A", 90)
            assert m.b(0x7E0067) == 11, "normal save-point screen"
            to_slot(m, 0)
            press(m, "A", 60)
            press(m, "A", 90)
            press(m, "B", 60)
            assert run.wait_control()
            assert m.b(0x7F00AF) & 7 == 7 and m.b(0x7F00BA) & 2
            assert m.b(0x7F00D2) == 0 and not m.b(0x7F00BA) & 0x80
            run.checkpoint("saved")
        destination = Path(REPO) / "oracle/saves/recorded/blackbird-before-dalton.srm"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(m.peek(0x306000, 8192))
        run.finish("a normal cartridge save after the Blackbird opening and equipment recovery")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    except (AssertionError, RuntimeError):
        m.save_state(args.prefix + "_failed.st")
        m.screenshot(args.prefix + "_failed.png")
        run.say("failed at", m.frame, "map", run.map_id())
        raise
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
