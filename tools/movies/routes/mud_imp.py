"""Beat the Mud Imp and Red and Blue Beasts from Terra Cave's nearest save.

ctsave09 slot 2 has story $AE, before the $B4 threshold that removes the boss
from $184. Slot 3 is already $B4, on Mt. Woe. The cave has two Beast pairs
before the boss; stop after the third victory with the field back in hand.
"""
from .. import policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/fantasyanime/ctsave09-epoch.srm"
PHASES = ["loaded", "cave", "beasts1", "beasts2", "battle", "won", "after"]
HEADER = """\
# Terra Cave, 12,000 B.C.: from the settlement save through two Beast pairs to the Mud Imp,
# Blue Beast and Red Beast; win and return control to the field (route mud_imp).
# Save: ctsave09 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def mud_imp(bot, state, rng, inventory):
    """Focus basic magic on the Imp; its defeat makes both Beasts leave."""
    member = state.member
    for slot in range(3):
        if not state.hp(slot):
            action = policies.use(inventory, policies.REVIVE, slot)
            if action:
                return action
    low = [s for s in range(3) if policies.fraction(state, s) < 0.35]
    if len(low) >= 2:
        action = policies.use(inventory, policies.LAPIS, None)
        if action:
            return action
    if low:
        action = policies.use(inventory, policies.MID_TONIC, min(low, key=lambda s: state.hp(s)))
        if action:
            return action
    if state.mp(member) < 8:
        action = policies.use(inventory, policies.MID_ETHER, member)
        if action:
            return action
    living = policies.enemies(state)
    if 9 in living and state.mp(member) >= 2:
        return ("tech", 2, 9)
    return ("attack", min(living, key=lambda s: state.hp(s)) if living else None)


def win(run, policy, seed):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(s) for s in range(3))

    run.fight(policy, won, seed=seed, max_frames=30000, max_tries=100)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
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
            m.run(300)
            assert run.wait_control()
            assert run.map_id() == "7c01" and m.b(0x7F0000) == 0xAE
            run.say("loaded", run.map_id(), "story", hex(m.b(0x7F0000)), "at", run.me())
            run.checkpoint("loaded")
        if start <= 1:
            assert run.exit_to("8401"), "the Beast cave"
            assert run.wait_control()
            run.checkpoint("cave")
        for phase, row in [(2, 47), (3, 29), (4, 17)]:
            if start <= phase:
                assert run.find_player() is not None
                assert run.walk(lambda machine, x, y: y < (row + 1) * 16) is not None
                assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 6000), "battle menu"
                if phase < 4:
                    win(run, "party_with_lapis", args.seed)
                    assert run.wait_control(10000)
                run.checkpoint(PHASES[phase])
        if start <= 5:
            win(run, mud_imp, args.seed)
            run.checkpoint("won")
        if start <= 6:
            assert run.wait_control(10000), "the field back in hand"
            assert run.map_id() == "8401" and m.b(0x7F0000) == 0xB4
            run.checkpoint("after")
        m.screenshot(run.prefix + "_end.png")
        run.finish("the Mud Imp and Beasts beaten")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if m.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
