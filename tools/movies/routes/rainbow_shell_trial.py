"""The Rainbow Shell trial, Yakra XIII, and Marle reunited with her father."""
from ..campaign import Run, route_arguments, run_route
from .. import policies

SAVE = "oracle/saves/fantasyanime/ctsave13-side_quests2.srm"
HEADER = """\
# The full Rainbow Shell trial at Guardia Castle, 1000 A.D. (route rainbow_shell_trial).
# Save: ctsave13 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["loaded", "castle", "stairs", "court_door", "first_trial", "treasury", "treasury_won",
          "fragment", "return_fight", "return_court", "yakra", "won", "after"]


def yakra_policy(bot, state, rng, inventory):
    if any(state.hp(i) < state.max_hp(i) * .3 for i in range(3)):
        return policies.party(bot, state, rng, inventory)
    member = state.member
    if member in (0, 2) and state.mp(0) < 12:
        return policies.use(inventory, policies.FULL_ETHER, 0) or ("attack", 3)
    if member == 0:
        return ("tech", 6, 3)  # Confuse
    if member == 1:
        return ("tech", 7, None)  # Flare
    return ("tech", 5, 3)  # Ice 2


def fight(run, seed, boss=False):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    def attack(bot, state, rng, inventory):
        living = policies.enemies(state)
        return ("attack", min(living, key=lambda i: state.hp(i)) if living else None)

    run.fight(yakra_policy if boss else attack, won, seed=seed, max_frames=20000, delays=(0,))


class TrialRun(Run):
    def wait_control(self, max_frames=6000, press_every=8):
        return super().wait_control(max_frames, press_every)

    def dialogs_until(self, condition, max_frames=20000, press_every=8):
        return super().dialogs_until(condition, max_frames, press_every)


def fast_messages(machine):
    from .menu_sweep import open_menu, open_option, SETTINGS, press
    open_menu(machine)
    open_option(machine, SETTINGS)
    machine.run(30)
    press(machine, "RIGHT")
    press(machine, "A", 40)
    for _ in range(4):
        press(machine, "LEFT")
    press(machine, "DOWN")
    for _ in range(4):
        press(machine, "LEFT")
    assert machine.b(0x7E0D8B) == 0 and machine.b(0x7E0D90) == 0
    for _ in range(3):
        press(machine, "B", 40)


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = TrialRun(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(1)
            m.run(4, "A")
            for _ in range(200):
                m.run(10)
                if run.map_id() == "f001":
                    break
            m.run(120)
            assert run.map_id() == "f001", "1000 A.D."
            assert m.b(0x7F00A1) & 0x40, "the shell delivered"
            assert not m.b(0x7F0050) & 0x40, "the trial unfinished"
            assert 1 in m.peek(0x7E2980, 3), "Marle in the party"
            fast_messages(m)
            run.checkpoint("loaded")
        if start <= 1:
            m.run(180)
            assert run.world_exit(max_nodes=500), "into the castle"
            assert run.map_id() == "1500", "Guardia Castle"
            assert run.wait_control(), "the castle in hand"
            run.checkpoint("castle")
        if start <= 2:
            assert run.exit_to("e801"), "the courtroom stairs"
            assert run.wait_control()
            run.checkpoint("stairs")
        if start <= 3:
            # Each landing is another part of the same location. Walk to the
            # upper edge of the doorway, then right to climb the next flight.
            for _ in range(3):
                m.run(12, "UP")
                m.run(32, "RIGHT")
                m.run(90)
            assert run.map_id() == "b901", "outside the courtroom"
            assert run.wait_control()
            run.checkpoint("court_door")
        if start <= 4:
            m.run(64, "DOWN")
            m.run(136, "RIGHT")
            m.run(12, "UP")
            m.run(8)
            m.run(3, "A")
            m.run(40)
            assert run.wait_control(16000), "Marle sent to find evidence"
            assert m.b(0x7F0069) == 2, "the trial's first visit"
            run.checkpoint("first_trial")
        if start <= 5:
            assert run.exit_to("e801")
            assert run.wait_control()
            for _ in range(3):
                m.run(12, "UP")
                m.run(32, "LEFT")
                m.run(90)
            assert run.map_id() == "1500"
            assert run.wait_control()
            assert run.exit_to("1a00"), "the treasury"
            assert run.wait_control()
            run.checkpoint("treasury")
        if start <= 6:
            assert run.walk(lambda m, x, y: m.b(0x7E0127) != 0, max_nodes=1200) is not None
            assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 5000)
            fight(run, args.seed)
            assert run.dialogs_until(lambda: run.map_id() == "b601", 6000), "the trial interlude"
            assert run.dialogs_until(lambda: run.map_id() == "1a00" and m.b(0x7E95D5) < 0x80, 6000)
            fight(run, args.seed)
            assert run.wait_control(12000)
            run.checkpoint("treasury_won")
        if start <= 7:
            assert run.exit_to("b801"), "the shell chamber"
            assert run.wait_control()
            for frames, button in [(64, "UP"), (60, 0), (110, "UP"), (30, 0), (620, "UP"), (60, 0),
                                   (8, "LEFT"), (100, "UP"), (8, 0), (3, "A"), (40, 0)]:
                m.run(frames, button)
            assert run.wait_control(10000), "the shell fragment collected"
            assert m.b(0x7F00A2) & 0x80, "the shell fragment flag"
            assert m.b(0x7F0069) == 0x10, "the court awaits the evidence"
            run.checkpoint("fragment")
        if start <= 8:
            for _ in range(150):
                if run.map_id() != "b801":
                    break
                m.run(8, "DOWN")
            m.run(120)
            assert run.map_id() == "1a00"
            assert run.wait_control()
            m.run(780, "DOWN")
            m.run(40)
            assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 3000)
            fight(run, args.seed)
            assert run.wait_control()
            run.checkpoint("return_fight")
        if start <= 9:
            m.run(720, "DOWN")
            m.run(144, "LEFT")
            m.run(108, "UP")
            m.run(40)
            assert run.exit_to("1500")
            assert run.wait_control()
            assert run.exit_to("e801")
            assert run.wait_control()
            for _ in range(3):
                m.run(12, "UP")
                m.run(32, "RIGHT")
                m.run(90)
            assert run.map_id() == "b901"
            assert run.wait_control()
            run.checkpoint("return_court")
        if start <= 10:
            m.run(64, "DOWN")
            m.run(136, "RIGHT")
            m.run(12, "UP")
            m.run(8)
            m.run(3, "A")
            m.run(40)
            assert run.dialogs_until(lambda: run.map_id() == "b601" and m.b(0x7E95D5) < 0x80, 10000)
            run.checkpoint("yakra")
        if start <= 11:
            fight(run, args.seed, boss=True)
            run.checkpoint("won")
        if start <= 12:
            assert run.wait_control(12000)
            assert m.b(0x7F0050) & 0x40, "the King exonerated"
            assert run.map_id() == "1500" and m.b(0x7F0069) == 0, "the castle field in hand"
            run.checkpoint("after")
        m.screenshot(run.prefix + "_end.png")
        run.finish("the Rainbow Shell trial and Yakra XIII")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
