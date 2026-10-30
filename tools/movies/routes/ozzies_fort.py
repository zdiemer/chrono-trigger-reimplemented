"""Ozzie's Fort in 600 A.D., from ctsave12 slot 2 to the final trap.

Slot 2 has $7F:01A1 = $00; slot 3 has $FF, including the final trap's bit 7,
and $7F:01A0 bit 7 set by the switch battle. Board the Epoch at the End of
Time, choose 600 A.D., and land east of the fort. The rooms are $0B1, $0B7
(Flea Plus), $0B2 (conveyor), $0B8 (Super Slash), $0B3 (treasure), $0B4 (the
three generals) and $0B5 (the switch battle and the cat's final trap).

Stop on each event's trigger row before waiting for its dialogue: searching
for an open window alone misses the animations that start before the window.
Leave the trapped chest alone; taking it leaves everyone at 1 HP. The trio
policy uses Lucca's Flare and lets the others heal between its counters.
Attack the switch in the last battle, climb back from the floor below, and
let the cat spring the final trap. Check all eight fort bits and field control.
"""
from .. import addresses as A, bfs, policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave12-side_quests1.srm"
HEADER = """\
# Ozzie's Fort, 600 A.D.: Flea Plus, Super Slash, the three generals and Ozzie's final trap.
# From the End of Time by Epoch; the conveyor and treasure scenes, the switch battle,
# the fall to the floor below, and the cat springing the final trap; field control restored (route ozzies_fort).
# Save: ctsave12 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["loaded", "epoch", "world", "fort", "flea", "flea_won", "conveyor", "slash", "slash_won",
          "treasure", "trio", "trio_won", "trap", "fallen", "after"]


def fly_to(run, x, y):
    machine = run.machine
    for axis, target in enumerate((x, y)):
        for _ in range(200):
            distance = target - run.world_pos()[axis]
            if abs(distance) <= 8:
                break
            direction = (("LEFT", "RIGHT"), ("UP", "DOWN"))[axis][distance > 0]
            machine.run(min(20, max(1, (abs(distance) - 6) // 2)), direction)
        else:
            raise AssertionError("the Epoch's destination")
        machine.run(10)


def win(run, seed, policy="party_with_lapis"):
    seen = []
    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))
    run.fight(policy, won, seed=seed, max_frames=25000)
    assert run.wait_control(8000), "the field back in hand"


def generals(bot, state, rng, inventory):
    """Lucca casts Flare; Crono and Marle heal between the trio's counters."""
    member = state.member
    character = policies.party_ids(bot)[member]
    if character == 2 and state.mp(member) >= 5:  # Flare with her Gold Stud
        return ("tech", 7, None)
    dead = [i for i in range(3) if not state.hp(i)]
    if dead:
        return policies.use(inventory, policies.REVIVE, dead[0]) or policies.party(bot, state, rng, inventory)
    low = min(range(3), key=lambda i: policies.fraction(state, i))
    if policies.fraction(state, low) < 0.80:
        if character == 1 and state.mp(member) >= 5:
            return ("tech", 6, low)  # Cure 2
        action = (policies.use(inventory, policies.ELIXIR, low)
                  or policies.use(inventory, policies.FULL_TONIC, low))
        if action:
            return action
    living = policies.enemies(state)
    target = 7 if 7 in living else (9 if 9 in living else 3)
    return ("attack", target) if character == 0 else ("tech", 2, target)


def main():
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
            run.select_slot(1)
            machine.run(4, "A")
            for _ in range(200):
                machine.run(10)
                if run.map_id() not in ("0000", "b101"):
                    break
            machine.run(120)
            assert machine.b(0x7F01A1) == 0, "the fort has not been visited"
            run.say("loaded", run.map_id(), run.world_pos(), "flags", machine.peek(0x7F01A0, 2).hex())
            run.checkpoint("loaded")
        if start <= 1:
            assert run.wait_control()
            assert run.go(520, 350, tolerance=8), "the right walkway"
            assert run.go(624, 380, tolerance=6), "the Epoch's steps"
            assert run.go(624, 394, tolerance=4), "the boarding point"
            machine.run(2, "DOWN")
            machine.run(8)
            machine.run(3, "A")
            assert run.dialogs_until(run.dialog, 300), "Board the Epoch?"
            machine.run(90)
            machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() == "d901" and run.dialog(), 3000)
            machine.run(90)
            machine.run(3, "A")
            machine.run(700)
            run.checkpoint("epoch")
        if start <= 2:
            for _ in range(8):
                if machine.b(0x7E0C52) == 4:
                    break
                machine.run(4, "DOWN")
                machine.run(56)
            assert machine.b(0x7E0C52) == 4, "600 A.D. on the dial"
            machine.run(3, "A")
            assert run.dialogs_until(lambda: run.map_id() == "f101", 3000)
            machine.run(300)
            assert machine.b(A.OVERWORLD_TRAVEL) == 2
            run.checkpoint("world")
        if start <= 3:
            fly_to(run, 1048, 408)
            def landing(m, x, y):
                m.mark(4000)
                m.probe(150)
                landed = m.b(A.OVERWORLD_TRAVEL) == 0
                m.restore(4000)
                m.free(4000)
                return landed
            moves, _ = bfs.ow_bfs(machine, landing,
                                  lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(),
                                  confirm=True, max_nodes=100)
            assert moves is not None, "ground beside Ozzie's Fort"
            machine.run(150)
            assert run.world_exit(max_nodes=300) and run.map_id() == "b100", "Ozzie's Fort"
            assert run.wait_control()
            run.checkpoint("fort")
        if start <= 4:
            assert run.exit_to("b700"), "Flea's room"
            assert run.wait_control()
            assert run.walk(lambda m, x, y: int(y) // 16 <= 40) is not None, "Flea's trigger row"
            assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "Flea's battle menu"
            run.checkpoint("flea")
        if start <= 5:
            win(run, args.seed)
            run.checkpoint("flea_won")
        if start <= 6:
            assert run.exit_to("b200"), "the conveyor room"
            assert run.wait_control()
            run.checkpoint("conveyor")
        if start <= 7:
            assert run.find_player() is not None
            assert run.walk(lambda m, x, y: int(y) // 16 <= 17) is not None, "the conveyor's trigger row"
            machine.run(60)
            assert run.wait_control(), "the conveyor trap over"
            assert run.exit_to("b800"), "Slash's room"
            assert run.wait_control()
            assert run.walk(lambda m, x, y: int(y) // 16 <= 40) is not None, "Slash's trigger row"
            assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "Slash's battle menu"
            run.checkpoint("slash")
        if start <= 8:
            win(run, args.seed + 1)
            run.checkpoint("slash_won")
        if start <= 9:
            assert run.exit_to("b300"), "the treasure trap room"
            assert run.wait_control()
            run.checkpoint("treasure")
        if start <= 10:
            assert run.find_player() is not None
            assert run.walk(lambda m, x, y: int(y) // 16 <= 40) is not None, "the treasure trap's introduction"
            machine.run(60)
            assert run.wait_control(), "Ozzie's treasure offer"
            assert run.go(168, 600, tolerance=6), "around the blade"
            assert not machine.b(0x7F01A1) & 0x10, "the chest left alone"
            assert run.walk(lambda m, x, y: int(y) // 16 <= 36) is not None, "past the trapped treasure"
            machine.run(60)
            assert run.wait_control(), "the treasure trap over"
            assert run.exit_to("b400"), "the three generals' room"
            assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "the trio's battle menu"
            run.checkpoint("trio")
        if start <= 11:
            win(run, args.seed + 3, policy=generals)
            run.checkpoint("trio_won")
        if start <= 12:
            assert run.exit_to("b500"), "Ozzie's final room"
            assert run.wait_control()
            assert run.walk(lambda m, x, y: int(y) // 16 <= 38) is not None, "Ozzie's trigger row"
            assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "Ozzie's battle menu"
            run.checkpoint("trap")
        if start <= 13:
            run.fight(lambda bot, state, rng, inventory: ("attack", 7),
                      lambda r, s: r.map_id() == "b400", max_frames=8000)
            assert run.wait_control(), "back on the floor below"
            run.checkpoint("fallen")
        if start <= 14:
            assert run.exit_to("b500"), "back to Ozzie"
            assert run.wait_control()
            assert run.walk(lambda m, x, y: int(y) // 16 <= 38) is not None, "the cat's trigger row"
            assert run.wait_control(8000), "the field after Ozzie's fall"
            assert machine.b(0x7F01A1) == 0xFF, "all fort events completed"
            run.checkpoint("after")
        if args.until is None:
            assert run.map_id() == "b500" and machine.b(0x7F01A1) == 0xFF
            assert run.find_player() is not None, "the completed fort in hand"
            run.finish("Ozzie's Fort cleared and the field back in hand")
            if args.movie:
                run.write_movie(args.movie, HEADER.splitlines())
        machine.screenshot(run.prefix + "_end.png")
    finally:
        if machine.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
