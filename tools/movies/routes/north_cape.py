"""Meet Magus at North Cape after the Blackbird and fight him with the party.

ctsave11 slot 2 has story progress $D2 and $7F:0138 clear; slot 3 and
ctsave12 already have recruitment bit 0 set. Marle, Lucca and Robo are active,
so this is the party fight, rather than Frog's duel. Machine copies keep the
walk searches and fight retries out of the movie. magus_joins uses the same
opening and refuses the fight instead.
"""
from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave11-crono_gone.srm"
SLOT = 1
CAPE = "ac01"
OUTCOME = 0x7F0138
CHOICE_ACTIVE, CHOICE_POINTER, DIALOG_STRING = 0x7E0162, 0x7E0163, 0x7E012A
PHASES = ["loaded", "world", "cape", "choice", "branch", "after"]
HEADER = """\
# North Cape, 12,000 B.C.: leave the Earthbound Village, meet Magus, and see his flashback.
# {BRANCH} The movie ends with the field back in hand (route {ROUTE}).
# Save: ctsave11 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main(join=False):
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(SLOT)
            machine.run(4, "A")
            machine.run(500)
            assert run.wait_control(), "the village in hand"
            assert run.map_id() == "a901", "Earthbound Village"
            assert machine.b(0x7F0000) == 0xD2 and machine.b(OUTCOME) == 0, "Magus still waiting"
            assert machine.peek(A.PARTY_MEMBERS, 3) == bytes((1, 2, 3)), "Marle, Lucca and Robo"
            run.checkpoint("loaded")
        if start <= 1:
            visited = set()
            for _ in range(5):
                if int(run.map_id()[2:] + run.map_id()[:2], 16) >= 0x1F0:
                    break
                here = run.map_id()
                assert run.exit_other(visited), "out of the village"
                visited.add(here)
                machine.run(120)
                run.wait_control(600)
            assert run.map_id() == "f601", "the surviving land in 12,000 B.C."
            run.checkpoint("world")
        if start <= 2:
            moves, _ = bfs.ow_bfs(machine, lambda m, x, y: m.peek(A.MAP_ID, 2).hex() == CAPE,
                                  lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(),
                                  confirm=True, max_nodes=1200)
            assert moves is not None, "North Cape's entrance"
            assert run.wait_control(), "North Cape in hand"
            run.checkpoint("cape")
        if start <= 3:
            run.find_player()
            for _ in range(60):
                if run.me()[1] <= 160:
                    break
                before = run.me()
                machine.run(8, "UP+B")
                assert run.me() != before, "the open path up the cape"
            tx, ty = run.object_pos(16)
            assert run.go(tx, ty + 18), "below the glimmer"
            machine.run(2, "UP")
            machine.run(8)
            machine.run(3, "A")
            assert run.dialogs_until(lambda: machine.b(CHOICE_ACTIVE) and machine.b(DIALOG_STRING) == 0x96,
                                     16000), "Magus's fight question after the flashback"
            machine.run(120)
            run.checkpoint("choice")
        if start <= 4:
            if join:
                machine.run(3, "DOWN")
                machine.run(20)
                assert machine.b(CHOICE_POINTER) == 2, "refuse the fight"
            else:
                assert machine.b(CHOICE_POINTER) == 1, "accept the fight"
            machine.run(3, "A")
            machine.run(30)
            if join:
                assert run.wait_control(), "walk away from Magus"
            else:
                assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 5000), "Magus battle menu"
            run.checkpoint("branch")
        if start <= 5:
            if join:
                assert run.find_player() is not None, "the leader after refusing the fight"
                assert run.go(120, 232), "walk away to invite Magus's offer"
                assert run.dialogs_until(lambda: machine.b(OUTCOME) & 1, 8000), "Magus joins"
                # Keep the default name, then close the party screen.
                machine.run(180)
                assert machine.b(0x7E0A00) == 3, "Magus's naming screen"
                machine.run(3, "START")
                machine.run(180)
                machine.run(3, "B")
                machine.run(120)
            else:
                seen = []

                def won(r, state):
                    living = policies.enemies(state)
                    if living:
                        seen.append(True)
                    return bool(seen) and not living and any(state.hp(i) for i in range(3))

                run.fight("party", won, party=3, seed=args.seed, max_frames=25000)
            assert run.wait_control(10000), "North Cape back in hand"
            assert machine.b(OUTCOME) & (1 if join else 2), "the branch's outcome flag"
            assert run.map_id() == CAPE, "still on North Cape"
            run.checkpoint("after")
        machine.screenshot(run.prefix + "_end.png")
        run.finish("Magus recruited" if join else "Magus beaten at North Cape")
        if args.movie:
            header = HEADER.replace("{BRANCH}", "Refuse the fight and recruit Magus." if join else
                                    "Marle, Lucca and Robo fight Magus and win.")
            run.write_movie(args.movie, header.replace("{ROUTE}", "magus_joins" if join else "north_cape").splitlines())
    finally:
        if machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
