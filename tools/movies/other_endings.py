"""Shared Lavos fights for the hand-run New Game+ ending routes.

These routes keep their movies in scratch storage: each fight and ending is
too long to replay in every gate. Checkpoints contain only the winning input.
"""
from .campaign import Run, route_arguments
from .routes.telepod_lavos import to_the_shell


TELEPOD_PHASES = ["fair", "exhibit", "shell", "cavern", "inner", "core", "ending"]


def lavos(run, start, *, party, seed):
    """The shell, cavern and two inner fights, shared by the ending routes."""
    policy = "crono_and_marle" if party == 2 else "party"
    if start <= 3:
        run.fight(policy, lambda r, s: r.map_id() == "da01", party=party, seed=seed,
                  idle=lambda bot: bot.step(22, "UP"))
        run.checkpoint("cavern")
    if start <= 4:
        assert run.wait_control(), "control inside Lavos"
        assert run.exit_to("db01"), "the inner cavern's second chamber"
        run.checkpoint("inner")
    if start <= 5:
        run.fight(policy, lambda r, s: r.map_id() == "df01", party=party, seed=seed)
        run.checkpoint("core")
    if start <= 6:
        def core_beaten(r, state):
            return (state.max_hp(9) == 30000 and state.hp(9) == 0
                    and any(state.hp(slot) for slot in range(party)))
        run.fight("crono_and_marle_core" if party == 2 else "party", core_beaten,
                  party=party, seed=seed)
        run.checkpoint("ending")


def telepod_ending(*, save, title, ending):
    args = route_arguments(title, save=save)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(TELEPOD_PHASES)
        to_the_shell(run, start)
        lavos(run, start, party=2, seed=args.seed)
        ending(run)
        run.machine.screenshot(run.prefix + "_end.png")
        if args.movie:
            run.write_movie(args.movie, [
                "# " + title + ": New Game+ telepod, Lavos shell, inner Lavos and Core, then the ending.",
                "# Save: " + save + " slot 2.",
                "# frames {FRAMES}", "# sram crc32 {CRC}",
                "# <start_frame> <frame_count> <buttons>"])
        run.finish(title)
    finally:
        # --until closes the run itself before raising RouteDone.
        if not run.logfile.closed:
            run.close()
