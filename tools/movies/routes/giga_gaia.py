"""Beat Giga Gaia, free Melchior and watch Mt. Woe fall, until Terra Cave is back in hand.

Ctsave09 slot 3 is on Mt. Woe ($189), story $B4, before the summit's
arrival flag ($7F:01A0 bit 3). The last bridge crosses the map's levels;
its northern exit needs UP held after reaching the end of the chain.
The route plays the movie's own way to the summit (battle-giga-gaia) on
time and starts from the battle; the playthrough walks it.
"""
from pathlib import Path

from .. import addresses as A, policies
from ..campaign import REPO, Run, route_arguments, run_route
from ._zeal_navigation import approach
from .playthrough import leave_fighting

SAVE = "saves/fantasyanime/ctsave09-epoch.srm"
MOVIE = Path(REPO) / "movies/saves/battle-giga-gaia.ctm"
# The movie's walk ends as the battle begins (its first command menu opens at frame 5752 in the reference).
WALK_ENDS = 5710
# The walk's phases (to giga_gaia) are the playthrough's, which climbs Mt. Woe
# (playthrough_leg-c); main starts at giga_gaia from the movie's walk.
PHASES = ["loaded", "woe_bridge", "woe_summit", "giga_gaia", "giga_gaia_won", "woe_fallen"]
HEADER = """\
# Mt. Woe: from the last save point over the bridges to Giga Gaia; defeat the head and arms,
# free Melchior, watch the mountain fall, then the Terra Cave conversation until control returns (route giga_gaia).
# Save: ctsave09 slot 3 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def fight_policy(bot, state, rng, inventory):
    """Heal with the save's supplies; area magic for the arms, Spincut for the head.

    Lucca has Fire 2 but not Flare on this save. Picking her eighth list row
    would try a combination tech instead, often before its partner is ready.
    """
    action = policies.party_with_lapis(bot, state, rng, inventory)
    if action[0] == "item" or any(state.hp(slot) == 0 for slot in range(3)):
        return action
    member = state.member
    arms = any(state.hp(slot) for slot in (7, 9))
    index, cost = (4, 8) if member == 0 and arms else ((3, 4) if member == 0 else (5, 8))
    if state.mp(member) >= cost:
        return ("tech", index, None if index in (4, 5) else 6)
    return policies.use(inventory, policies.ETHER, member) or ("attack", 6)


def giga_gaia_ready(machine):
    """Giga Gaia's battle at its first command menu, the head at its full 9,500 HP."""
    return A.battle_menu_open(machine) and machine.w(A.BATTLE_STATS + 6 * 128 + 2) == 9500


def play(run, start=1, seed=1):
    """From Mt. Woe's last save point to Giga Gaia beaten and Terra Cave in
    hand; `start` is the index in PHASES of the first phase to run."""
    machine = run.machine
    if start <= 1:
        # Mt. Woe's monsters jump a party that comes up from below.
        assert leave_fighting(run, "8b01", seed=seed), "the bridge"
        assert run.wait_control()
        run.checkpoint("woe_bridge")
    if start <= 2:
        approach(run, 968, 32)
        machine.run(120, "UP")
        assert run.map_id() == "8d01"
        assert run.wait_control()
        run.checkpoint("woe_summit")
    if start <= 3:
        assert run.wait_control()
        machine.run(100, "UP")
        machine.run(3, "A")
        assert run.dialogs_until(lambda: giga_gaia_ready(machine), 8000)
        run.checkpoint("giga_gaia")
    if start <= 4:
        seen = []

        def won(r, state):
            living = policies.enemies(state)
            if living:
                seen.append(True)
            return bool(seen) and not living and any(state.hp(slot) for slot in range(3))

        run.fight(fight_policy, won, seed=seed, max_frames=28000, max_tries=100)
        run.checkpoint("giga_gaia_won")
    if start <= 5:
        assert run.wait_control(16000), "the field after Mt. Woe's fall"
        assert run.map_id() == "7e01" and machine.b(0x7F0000) == 0xBA
        run.checkpoint("woe_fallen")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)
    try:
        if start <= 3:
            # The way to the summit is the movie's own, which the field plays on time.
            machine.play_movie(str(MOVIE), WALK_ENDS)
            assert run.dialogs_until(lambda: giga_gaia_ready(machine), 3000), "Giga Gaia's battle"
            run.checkpoint("giga_gaia")
        play(run, max(start, 4), args.seed)
        run.finish("Giga Gaia beaten, Melchior freed and Mt. Woe fallen")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
