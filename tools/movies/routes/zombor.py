"""Deliver the supplies and defeat Zombor on Zenan Bridge."""
from .. import bfs, policies
from ..campaign import Run, route_arguments, run_route
from . import _early_bosses as early

SAVE = "oracle/saves/fantasyanime/ctsave04-masamune.srm"
SLOT = 0
PHASES = ["loaded", "settings", "bridge", "supplies", "defense", "first", "second", "battle", "after"]
HEADER = """# Zenan Bridge: deliver the supplies, defeat Ozzie's troops and Zombor, regain the field.
# Save: ctsave04 slot 1 (oracle/saves/fantasyanime/ctsave04-masamune.srm).
# Route: zombor
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def next_battle(run):
    # Soldiers obstruct the narrow bridge after each formation breaks up.
    for _ in range(8):
        assert run.wait_control()
        moves, visited = bfs.bfs_walk(run.machine,
            lambda m, x, y: not m.b(early.PLAYER_CONTROL),
            run._pos(), lambda m: (run.map_id(), m.b(early.PLAYER_CONTROL)),
            step=16, cell=6, max_nodes=3000)
        run.say("bridge approach", None if moves is None else len(moves), "steps", len(visited), "visited")
        assert moves is not None, "the next bridge encounter"
        assert run.dialogs_until(lambda: early.battle_ready(run) or run.machine.b(early.PLAYER_CONTROL), 12000)
        if early.battle_ready(run):
            return
    raise AssertionError("too many bridge scenes")


def bridge_party(bot, state, rng, inventory):
    action = early.early_party(bot, state, rng, inventory)
    if action[0] == "item":
        return action
    skeletons = [s for s in policies.enemies(state) if state.max_hp(s) == 110]
    if skeletons and state.mp(state.member) >= 2 and policies.tech_known(bot, state.member, 2):
        return ("tech", 2, skeletons[0])
    return ("attack", None)


def zombor_party(bot, state, rng, inventory):
    action = early.early_party(bot, state, rng, inventory)
    if action[0] == "item":
        return action
    living = policies.enemies(state)
    if not living:
        return ("attack", None)
    character = policies.party_ids(bot)[state.member]
    upper = next((s for s in living if state.max_hp(s) == 960), None)
    lower = next((s for s in living if state.max_hp(s) == 800), None)
    target = upper if upper is not None else lower
    if state.mp(state.member) >= 2 and policies.tech_known(bot, state.member, 2):
        if character == 1 and upper is not None:
            return ("tech", 2, upper)
        if character in (0, 2) and lower is not None:
            return ("tech", 2, lower)
    return ("attack", target if target is not None else living[0])


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, "f101", 0x54)
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        if start <= 2:
            assert run.world_exit()
            assert run.map_id() in ("8600", "8700"), "Zenan Bridge"
            assert run.wait_control()
            run.checkpoint("bridge")
        if start <= 3:
            assert run.talk(13), "give the supplies to the captain"
            assert run.wait_control()
            assert run.machine.b(early.STORY) == 0x57
            run.checkpoint("supplies")
        if start <= 4:
            assert run.talk(13), "agree to defend the bridge"
            assert run.wait_control()
            assert run.machine.b(0x7F019A) & 1
            run.checkpoint("defense")
        if start <= 5:
            next_battle(run)
            early.fight(run, seed=args.seed, policy=bridge_party)
            run.checkpoint("first")
        if start <= 6:
            next_battle(run)
            early.fight(run, seed=args.seed, policy=bridge_party)
            run.checkpoint("second")
        if start <= 7:
            next_battle(run)
            assert run.machine.b(early.STORY) == 0x5A
            run.checkpoint("battle")
        if start <= 8:
            early.fight(run, seed=args.seed, policy=zombor_party)
            assert run.machine.b(early.STORY) == 0x5A
            run.checkpoint("after")
        run.finish("Zombor defeated and Zenan Bridge in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
