"""Recover the Gate Key after defeating Nizbel in the Reptite Lair."""
from .. import policies
from ..campaign import Run, route_arguments, run_route
from . import _early_bosses as early

SAVE = "saves/fantasyanime/ctsave05-prehistoric.srm"
SLOT = 1
FOREST_MAZE = "1a01"
PHASES = ["loaded", "settings", "entrance", "top", "middle", "lower", "bottom", "lair_hall", "chamber", "battle", "won", "after"]
HEADER = """# Reptite Lair: defeat Nizbel, recover the Gate Key, regain field control.
# Save: ctsave05 slot 2 (saves/fantasyanime/ctsave05-prehistoric.srm).
# Route: nizbel
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def nizbel_party(bot, state, rng, inventory):
    member = state.member
    character = policies.party_ids(bot)[member]
    members = [s for s in range(3) if state.max_hp(s)]
    if not members:
        return ("attack", None)
    if character == 0 and state.mp(member) < 2:
        for item in (policies.ETHER, policies.MID_ETHER, policies.FULL_ETHER, policies.ELIXIR):
            action = early.battle_item(bot, item, member)
            if action:
                return action
    for target in members:
        if not state.hp(target):
            action = early.battle_item(bot, policies.REVIVE, target)
            if action:
                return action
    target = min(members, key=lambda s: policies.fraction(state, s))
    if policies.fraction(state, target) < 0.55:
        if character == 1 and state.mp(member) >= 1:
            return ("tech", 0, target)
        for item in (policies.MID_TONIC, policies.TONIC):
            action = early.battle_item(bot, item, target)
            if action:
                return action
    living = policies.enemies(state)
    if not living:
        return ("attack", None)
    if character == 0:
        if state.mp(member) >= 2:
            return ("tech", 2, living[0])
    if character == 5 and state.max_hp(living[0]) > 4000 and state.mp(member) >= 3 \
            and policies.tech_known(bot, member, 2):
        return ("tech", 2, living[0])
    return ("attack", living[0])


def descend(run, hole, destination, seed):
    for _ in range(6):
        assert run.wait_control()
        tx, ty = run.object_pos(hole)
        early.search_through(run, lambda m, x, y: abs(x - (tx + 18)) <= 3 and abs(y - ty) <= 3,
                             seed=seed, policy=nizbel_party)
        run.SPOTS = [(18, 0, "LEFT"), (0, -18, "DOWN"), (-18, 0, "RIGHT"), (0, 18, "UP")]
        assert run.talk(hole, done=lambda: not run.machine.b(early.PLAYER_CONTROL)), "enter the hole"
        assert run.dialogs_until(lambda: early.battle_ready(run) or run.map_id() == destination, 5000)
        if run.map_id() == destination:
            assert run.wait_control()
            return
        early.fight(run, seed=seed, policy=nizbel_party)
    raise AssertionError("the hole remains blocked")


def play(run, start=2, seed=1):
    """From the world map outside the Reptite Lair to Nizbel beaten, the Gate Key
    recovered and the field in hand at Ioka; `start` is the index in PHASES of the
    first phase to run."""
    if start <= 2:
        # The lair's door, not the Forest Maze's foot beside it, which leg B
        # of the playthrough comes out of.
        assert run.world_exit(avoid=frozenset({FOREST_MAZE}))
        assert run.wait_control()
        run.say("lair entrance", run.map_id(), run.me())
        run.checkpoint("entrance")
    if start <= 3:
        assert run.exit_to("1c01"), "the top floor"
        run.checkpoint("top")
    if start <= 4:
        # Reptites can spring from the hole as the party jumps (by when it
        # does): win, and jump again.
        for _ in range(6):
            assert run.talk(14, done=lambda: not run.machine.b(early.PLAYER_CONTROL)), "the open hole"
            assert run.dialogs_until(lambda: early.battle_ready(run) or run.map_id() == "1d01", 3000)
            if run.map_id() == "1d01":
                break
            early.fight(run, seed=seed, policy=nizbel_party)
        assert run.map_id() == "1d01", "the middle floor"
        assert run.wait_control()
        run.checkpoint("middle")
    if start <= 5:
        descend(run, 20, "1e01", seed)
        run.checkpoint("lower")
    if start <= 6:
        descend(run, 21, "1f01", seed)
        run.checkpoint("bottom")
    if start <= 7:
        early.search_through(run, lambda m, x, y: run.map_id() == "2001", seed=seed,
                             inside=lambda x, y: run.map_id() != "1f01" or y < 800, policy=nizbel_party)
        early.settle(run, seed, nizbel_party)
        assert run.wait_control(), "the hall after its entrance encounter"
        run.checkpoint("lair_hall")
    if start <= 8:
        early.search_through(run, lambda m, x, y: run.map_id() == "2101", seed=seed, policy=nizbel_party)
        early.settle(run, seed, nizbel_party)
        assert run.wait_control(), "inside Nizbel's chamber"
        run.checkpoint("chamber")
    if start <= 9:
        early.walk_through(run, "LEFT", lambda: run.me()[0] < 872, seed=seed)
        early.approach_battle(run, "UP")
        run.checkpoint("battle")
    if start <= 10:
        early.fight(run, seed=seed, policy=nizbel_party, after=False)
        run.checkpoint("won")
    if start <= 11:
        assert run.dialogs_until(lambda: 0x7C <= run.machine.b(early.STORY) <= 0x7D
                                 and run.map_id() == "1301", 16000), "recover the Gate Key"
        assert run.wait_control(), "the field after the return to Ioka"
        assert run.machine.b(early.STORY) == 0x7D, "Ayla's farewell"
        assert run.map_id() == "1301", "the return to Ioka"
        assert 0xD7 in run.machine.peek(0x7E2400, 0xF2), "the Gate Key in the bag"
        run.checkpoint("after")


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, "f301", 0x7B)
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        play(run, start, args.seed)
        run.finish("Nizbel defeated, Gate Key recovered and field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
