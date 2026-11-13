"""Defeat the Dragon Tank during the escape from Guardia Castle."""
from .. import policies
from ..campaign import Run, route_arguments, run_route
from . import _trial_party as early

SAVE = "saves/fantasyanime/ctsave02-future.srm"
SLOT = 0
PHASES = ["loaded", "settings", "bridge", "tank", "tank_won"]
HEADER = """# Guardia Castle escape: Crono and Lucca defeat the Dragon Tank, then regain the field.
# Save: ctsave02 slot 1 (saves/fantasyanime/ctsave02-future.srm).
# Route: dragon_tank
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def tank_party(bot, state, rng, inventory):
    action = early.early_party(bot, state, rng, inventory)
    if action[0] == "item":
        return action
    living = policies.enemies(state)
    # The 600 HP head repairs the other parts and resists elemental attacks.
    return ("attack", max(living, key=state.max_hp) if living else None)


def play(run, start=2, seed=1):
    """The bridge and the Dragon Tank, Crono and Lucca escaping the castle;
    `start` is the index in PHASES of the first phase to run."""
    if start <= 2:
        assert run.exit_to("1c00"), "the castle bridge"
        run.checkpoint("bridge")
    if start <= 3:
        early.approach_battle(run, "LEFT")
        run.checkpoint("tank")
    if start <= 4:
        early.fight(run, seed=seed, party=2, policy=tank_party)
        assert run.machine.b(0x7F0199) & 4, "the Dragon Tank is destroyed"
        run.checkpoint("tank_won")


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, "1d00", 0x2E)
            assert not run.machine.b(0x7F0199) & 4, "the Dragon Tank is still intact"
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        play(run, start, args.seed)
        run.finish("the Dragon Tank destroyed and the field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
