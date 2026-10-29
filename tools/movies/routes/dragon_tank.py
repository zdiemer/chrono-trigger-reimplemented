"""Defeat the Dragon Tank during the escape from Guardia Castle."""
from .. import policies
from ..campaign import Run, route_arguments, run_route
from . import _early_bosses as early

SAVE = "oracle/saves/fantasyanime/ctsave02-future.srm"
SLOT = 0
PHASES = ["loaded", "settings", "bridge", "battle", "after"]
HEADER = """# Guardia Castle escape: Crono and Lucca defeat the Dragon Tank, then regain the field.
# Save: ctsave02 slot 1 (oracle/saves/fantasyanime/ctsave02-future.srm).
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


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            early.load(run, SLOT, "1d00", 0x2E)
            assert not run.machine.b(0x7F0199) & 4, "the Dragon Tank is still intact"
            run.checkpoint("loaded")
        if start <= 1:
            early.fast_settings(run)
            run.checkpoint("settings")
        if start <= 2:
            assert run.exit_to("1c00"), "the castle bridge"
            run.checkpoint("bridge")
        if start <= 3:
            early.approach_battle(run, "LEFT")
            run.checkpoint("battle")
        if start <= 4:
            early.fight(run, seed=args.seed, party=2, policy=tank_party)
            assert run.machine.b(0x7F0199) & 4, "the Dragon Tank is destroyed"
            run.checkpoint("after")
        run.finish("the Dragon Tank destroyed and the field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
