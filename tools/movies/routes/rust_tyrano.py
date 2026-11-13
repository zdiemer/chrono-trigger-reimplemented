"""Defeat Rust Tyrano in Giant's Claw, ending with the field back in hand.

rainbow_shell makes the starting cartridge save by playing ctsave13 slot 1
to the dungeon's save point, using a Shelter and saving normally. Its long
approach is a hand-run route, outside the movie library. From there this
route plays the movie's own way into the chamber (battle-rust-tyrano) on
time: the door opened, Rust Tyrano's chamber entered and the battle begun.
"""
from pathlib import Path

from .. import addresses as A, policies
from ..campaign import REPO, Run, route_arguments, run_route
from .rainbow_shell import fight

SAVE = "saves/routes/rainbow-shell.srm"
MOVIE = Path(REPO) / "movies/saves/battle-rust-tyrano.ctm"
# The movie's walk ends as the battle begins (its first command menu opens at frame 4812 in the reference).
WALK_ENDS = 4770
PHASES = ["battle", "after"]
HEADER = """# Giant's Claw, 600 A.D.: from the dungeon save point, open the door,
# enter Rust Tyrano's chamber and defeat it with Crono, Lucca and Robo.
# Includes the countdown and fire breath; ends with the field back in hand.
# Route: rust_tyrano. The long approach is the hand-run route rainbow_shell.
# Save: rainbow-shell slot 1 (saves/routes/rainbow-shell.srm), saved
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
              until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            # The way to the chamber is the movie's own, which the field plays on time.
            m.play_movie(str(MOVIE), WALK_ENDS)
            assert run.dialogs_until(lambda: A.battle_menu_open(m), 3000), "Rust Tyrano's battle"
            m.run(700)                 # the party and Tyrano take their places
            run.checkpoint("battle")
        if start <= 1:
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
