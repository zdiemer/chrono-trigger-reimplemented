"""Rescue Queen Leene from Yakra in the Cathedral, 600 A.D."""
from .. import bfs
from ..campaign import Run, route_arguments, run_route
from . import _early_bosses as early

SAVE = "oracle/saves/fantasyanime/ctsave01-kidnapping.srm"
SLOT = 1
HEADER = """# Rescue Queen Leene: the Cathedral, Frog joins, the organs and switches, Yakra defeated.
# Save: ctsave01 slot 2 (oracle/saves/fantasyanime/ctsave01-kidnapping.srm).
# Route: yakra
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["loaded", "settings", "cathedral", "nun", "nagas", "frog", "organ", "hall", "inner",
          "right_switch", "organ_room", "inner_organ", "corridor", "room", "battle", "after"]


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, "f101", 0x12)
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        if start <= 2:
            assert run.world_exit(), "the Cathedral entrance"
            assert run.map_id() == "8100", "the Cathedral"
            assert run.wait_control(), "inside the Cathedral"
            run.checkpoint("cathedral")
        if start <= 3:
            assert run.find_player() is not None
            assert run.talk(12), "the nun reveals Leene's pin"
            run.checkpoint("nun")
        if start <= 4:
            assert run.talk(15), "examine Leene's pin"
            assert run.dialogs_until(lambda: run.machine.b(early.BATTLE_MENU_MEMBER) < 0x80, 8000)
            run.checkpoint("nagas")
        if start <= 5:
            early.fight(run, seed=args.seed, party=2, after=False)
            # Frog's naming is a menu, not a field dialogue window.
            assert run.dialogs_until(lambda: run.machine.b(0x7E0A00) == 3
                                     and run.machine.b(0x7E0067) == 13, 8000)
            run.machine.run(90)
            run.machine.run(3, "START")
            assert run.wait_control()
            assert run.machine.b(early.STORY) == 0x15, "Frog has joined"
            run.checkpoint("frog")
        if start <= 6:
            assert run.find_player() is not None
            # The organ's collision stops the leader 26 pixels below its
            # origin; approach from the aisle and press against its keys.
            moves, _ = bfs.bfs_walk(run.machine, lambda m, x, y: abs(x - 46) < 8 and abs(y - 164) < 8,
                                    run._pos(), run._map(), max_nodes=1000)
            assert moves is not None, "below the organ"
            run.machine.run(30, "UP")
            run.machine.run(8)
            run.machine.run(3, "A")
            assert run.dialogs_until(lambda: run.machine.b(0x7F00FF) & 0x10, 1200), "the organ's secret door"
            assert run.wait_control()
            run.checkpoint("organ")
        if start <= 7:
            assert run.exit_to("8200"), "the Cathedral's inner hall"
            run.checkpoint("hall")
        if start <= 8:
            early.walk_through(run, "UP", lambda: run.me()[1] <= 514, seed=args.seed)
            # The first pair attacks on this stair. Let the script finish
            # taking control before turning into the corridor below it.
            if not run.machine.b(early.PLAYER_CONTROL):
                early.fight(run, seed=args.seed)
            early.walk_through(run, "DOWN", lambda: run.me()[1] >= 540, seed=args.seed)
            early.walk_through(run, "LEFT", lambda: run.me()[0] <= 632, seed=args.seed)
            early.walk_through(run, "UP", lambda: run.map_id() == "8300", seed=args.seed)
            assert run.wait_control()
            run.checkpoint("inner")
        if start <= 9:
            # Bias the encounter-aware search toward the right staircase.
            early.walk_through(run, "RIGHT", lambda: run.me()[0] >= 432, seed=args.seed)
            early.search_through(run, lambda m, x, y: x > 500 and y < 355, seed=args.seed)
            # This switch clears the spikes before the inner organ.
            if run.me()[0] < 552:
                early.walk_through(run, "RIGHT", lambda: run.me()[0] >= 552, seed=args.seed)
            early.walk_through(run, "LEFT", lambda: run.me()[0] <= 552, seed=args.seed)
            early.walk_through(run, "UP", lambda: run.me()[1] <= 323, seed=args.seed)
            run.machine.run(8)
            run.machine.run(3, "A")
            assert run.dialogs_until(lambda: run.machine.b(0x7F00FF) & 2, 2000)
            assert run.wait_control()
            run.checkpoint("right_switch")
        if start <= 10:
            early.walk_through(run, "RIGHT", lambda: run.me()[0] >= 592, seed=args.seed)
            early.walk_through(run, "UP", lambda: run.me()[1] <= 144, seed=args.seed, interact=True)
            early.walk_through(run, "LEFT", lambda: run.me()[0] <= 112, seed=args.seed, interact=True)
            early.walk_through(run, "DOWN", lambda: run.me()[1] >= 319, seed=args.seed, interact=True)
            early.walk_through(run, "RIGHT", lambda: run.me()[0] >= 152, seed=args.seed)
            run.machine.run(12, "UP")
            run.machine.run(8)
            run.machine.run(3, "A")
            assert run.dialogs_until(lambda: run.machine.b(0x7F00FF) & 8, 2000)
            assert run.wait_control()
            early.search_through(run, lambda m, x, y: abs(x - 352) < 8 and 350 < y < 400,
                                 seed=args.seed, inside=lambda x, y: y > 300)
            run.checkpoint("organ_room")
        if start <= 11:
            early.walk_through(run, "LEFT", lambda: run.me()[0] <= 272, seed=args.seed)
            early.walk_through(run, "UP", lambda: run.me()[1] <= 331, seed=args.seed)
            run.machine.run(8)
            run.machine.run(3, "A")
            assert run.dialogs_until(lambda: run.machine.b(0x7F00FF) & 4, 2000)
            assert run.wait_control()
            run.checkpoint("inner_organ")
        if start <= 12:
            early.search_through(run, lambda m, x, y: abs(x - 352) < 8 and y < 160, seed=args.seed)
            early.walk_through(run, "UP", lambda: run.map_id() == "8400", seed=args.seed)
            assert run.wait_control()
            run.checkpoint("corridor")
        if start <= 13:
            early.walk_through(run, "UP", lambda: run.map_id() == "c600", seed=args.seed)
            assert run.wait_control()
            run.checkpoint("room")
        if start <= 14:
            assert early.advance(run, "UP", lambda: not run.machine.b(early.PLAYER_CONTROL))
            assert run.dialogs_until(lambda: run.machine.b(early.BATTLE_MENU_MEMBER) < 0x80, 8000)
            run.checkpoint("battle")
        if start <= 15:
            early.fight(run, seed=args.seed)
            assert run.machine.b(0x7F01ED) == 1, "Yakra defeated and Leene rescued"
            run.checkpoint("after")
        run.finish("Yakra defeated, Queen Leene rescued and the field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
