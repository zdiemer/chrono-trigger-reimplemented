"""Win the Golem Twins, then make the cartridge save used by ocean_palace_lavos."""
from pathlib import Path
from .save_point import press, to_slot
from .. import policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave10-ocean_palace.srm"
HEADER = """\
# Ocean Palace: Crono, Marle and Lucca win the Golem Twins with alternating Lightning 2, Ice 2 and Fire 2.
# Dalton leaves; return to the nearby save point and save slot 3, then close the menus (route golem_twins).
# Save: ctsave10 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["loaded", "battle", "won", "after_twins", "save_room", "saved", "after"]


def twins(bot, state, rng, inventory):
    """Cycle all-enemy elements to interrupt copying; keep MP for the next cast."""
    member = state.member
    dead = [i for i in range(3) if not state.hp(i)]
    if dead:
        return policies.use(inventory, policies.REVIVE, dead[0]) or ("tech", 5 if member == 0 else 7, dead[0])
    low = [i for i in range(3) if state.hp(i) < 250]
    if len(low) >= 2:
        action = policies.use(inventory, policies.LAPIS, None)
        if action:
            return action
    if state.hp(member) < 150:
        action = policies.use(inventory, policies.FULL_TONIC, member)
        if action:
            return action
    if state.mp(member) < 15:
        return policies.use(inventory, policies.FULL_ETHER, member)
    return ("tech", (4, 5, 5)[member], None)


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(2)
            machine.run(4, "A")
            machine.run(700)
            assert run.wait_control(), "the party at the save point"
            run.say("loaded", run.map_id(), run.me())
            run.checkpoint("loaded")
        if start <= 1:
            machine.run(40, "RIGHT")
            machine.run(300, "UP")
            assert run.dialogs_until(lambda: machine.b(0x7E95D5) < 0x80, 8000), "the Twins' menu"
            run.checkpoint("battle")
        if start <= 2:
            seen = []

            def won(r, state):
                living = policies.enemies(state)
                if living:
                    seen.append(True)
                return bool(seen) and not living and any(state.hp(i) for i in range(3))

            run.fight(twins, won, seed=args.seed, max_frames=24000, max_tries=30)
            run.checkpoint("won")
        if start <= 3:
            assert run.wait_control(12000), "the field after the Twins"
            run.say("after Twins", run.map_id(), run.me())
            run.checkpoint("after_twins")
        if start <= 4:
            assert run.exit_to("9d01"), "back to the save point room"
            assert run.wait_control(), "the save point room in hand"
            assert run.go(216, 111), "stand on the save point"
            run.checkpoint("save_room")
        if start <= 5:
            machine.run(30)
            for _ in range(3):
                press(machine, "A", 90)
                if machine.b(0x7E0067) == 11:
                    break
            machine.screenshot(run.prefix + "_save.png")
            assert machine.b(0x7E0067) == 11, "the save point screen"
            to_slot(machine, 2)
            press(machine, "A", 60)
            press(machine, "A", 90)
            run.checkpoint("saved")
        if start <= 6:
            press(machine, "B", 30)
            assert run.wait_control(), "the field after saving"
            run.checkpoint("after")
        # Battery RAM only: a reproducible game save, never a machine snapshot.
        Path(run.prefix + ".srm").write_bytes(machine.peek(0x306000, 8192))
        machine.screenshot(run.prefix + "_end.png")
        run.finish("the Golem Twins beaten and slot 3 saved")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
