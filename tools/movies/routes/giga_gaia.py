"""Beat Giga Gaia, free Melchior and watch Mt. Woe fall, until Terra Cave is back in hand.

Ctsave09 slot 3 is on Mt. Woe ($189), story $B4, before the summit's
arrival flag ($7F:01A0 bit 3). The last bridge crosses the map's levels;
its northern exit needs UP held after reaching the end of the chain.
"""
from .. import addresses as A, policies
from ..campaign import Run, route_arguments, run_route
from ._zeal_navigation import approach

SAVE = "oracle/saves/fantasyanime/ctsave09-epoch.srm"
PHASES = ["loaded", "bridge", "summit", "battle", "won", "after"]
HEADER = """\
# Mt. Woe: from the last save point over the bridges to Giga Gaia; defeat the head and arms,
# free Melchior, watch the mountain fall, then the Terra Cave conversation until control returns (route giga_gaia).
# Save: ctsave09 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
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


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)
    try:
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(2)
            machine.run(4, "A")
            machine.run(300)
            assert run.map_id() == "8901" and machine.b(0x7F0000) == 0xB4
            assert machine.peek(A.PARTY_MEMBERS, 3) == bytes((0, 1, 2)), "Crono, Marle and Lucca"
            assert not machine.b(0x7F01A0) & 8, "Giga Gaia still ahead"
            assert run.wait_control()
            run.checkpoint("loaded")
        if start <= 1:
            assert run.exit_to("8b01")
            assert run.wait_control()
            run.checkpoint("bridge")
        if start <= 2:
            approach(run, 968, 32)
            machine.run(120, "UP")
            assert run.map_id() == "8d01"
            assert run.wait_control()
            run.checkpoint("summit")
        if start <= 3:
            assert run.wait_control()
            machine.run(100, "UP")
            machine.run(3, "A")
            assert run.dialogs_until(lambda: machine.b(A.BATTLE_MENU_MEMBER) < 0x80
                                     and machine.w(A.BATTLE_STATS + 6 * 128 + 2) == 9500, 8000)
            run.checkpoint("battle")
        if start <= 4:
            seen = []

            def won(r, state):
                living = policies.enemies(state)
                if living:
                    seen.append(True)
                return bool(seen) and not living and any(state.hp(slot) for slot in range(3))

            run.fight(fight_policy, won, seed=args.seed, max_frames=28000, max_tries=100)
            run.checkpoint("won")
        if start <= 5:
            assert run.wait_control(16000), "the field after Mt. Woe's fall"
            assert run.map_id() == "7e01" and machine.b(0x7F0000) == 0xBA
            run.checkpoint("after")
        run.finish("Giga Gaia beaten, Melchior freed and Mt. Woe fallen")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
