"""Defeat Rust Tyrano in Giant's Claw, ending with the field back in hand.

rainbow_shell makes the starting cartridge save by playing ctsave13 slot 1
to the dungeon's save point, using a Shelter and saving normally. Its long
approach is a hand-run route, outside the movie library.
"""
from .. import addresses as A, policies
from ..campaign import Run, route_arguments, run_route
from .rainbow_shell import CONTROL, enter, fight, load

SAVE = "oracle/saves/routes/rainbow-shell.srm"
PHASES = ["loaded", "door", "room", "battle", "after"]
HEADER = """# Giant's Claw, 600 A.D.: from the dungeon save point, open the door,
# enter Rust Tyrano's chamber and defeat it with Crono, Lucca and Robo.
# Includes the countdown and fire breath; ends with the field back in hand.
# Route: rust_tyrano. The long approach is the hand-run route rainbow_shell.
# Save: rainbow-shell slot 1 (oracle/saves/routes/rainbow-shell.srm), saved
# normally after playing from fantasyanime ctsave13 slot 1, without pokes.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def tyrano(bot, state, rng, inventory):
    """Confuse and Uzzi Punch; keep the party healed and both attackers' MP up."""
    member = state.member
    # Keep Life 2 available when a member falls. Spending every turn on
    # revival items with Crono out of MP can leave the party in a death loop.
    if any(state.hp(i) == 0 for i in range(3)) and state.hp(0) and state.mp(0) < 10:
        for item in (policies.FULL_ETHER, policies.HYPER_ETHER, policies.ELIXIR, policies.MID_ETHER):
            action = policies.use(inventory, item, 0)
            if action:
                return action
    action = policies.survival(state, rng, inventory, member, range(3), self_threshold=.55,
                               life=(5, 10) if member == 0 else None)
    if action:
        return action
    if member == 2 and state.mp(2) >= 3 and sum(policies.fraction(state, i) < .8 for i in range(3)) >= 2:
        return ("tech", 4, None)       # Heal Beam
    for target in (0, 2):
        if state.mp(target) < 12:
            for item in (policies.FULL_ETHER, policies.HYPER_ETHER, policies.ELIXIR, policies.MID_ETHER):
                action = policies.use(inventory, item, target)
                if action:
                    return action
    living = policies.enemies(state)
    if not living:
        return ("attack", None)
    target = min(living, key=state.hp)
    if member in (0, 2) and state.mp(member) >= 12:
        return ("tech", 6 if member == 0 else 5, target)
    return ("attack", target)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            load(run)
            assert run.wait_control()
            assert run.map_id() == "6d00"
            assert not m.b(0x7F01D2) & 0x40
            run.checkpoint("loaded")
        if start <= 1:
            assert run.go(824, 354)
            m.run(2, "UP")
            m.run(8)
            m.run(30, "A")
            assert run.wait_control()
            assert m.b(0x7F0218) == 1, "the door opened"
            run.checkpoint("door")
        if start <= 2:
            enter(run, "c500")
            run.checkpoint("room")
        if start <= 3:
            assert run.wait_control()
            assert run.walk(lambda m, x, y: not m.b(CONTROL)) is not None
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 0x80, 5000)
            m.run(700)                 # the party and Tyrano take their places
            run.checkpoint("battle")
        if start <= 4:
            fight(run, tyrano, args.seed)
            assert run.map_id() == "c500" and m.b(0x7F01D2) & 0x40
            run.checkpoint("after")
        m.screenshot(run.prefix + "_end.png")
        run.finish("Rust Tyrano beaten, field back in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
