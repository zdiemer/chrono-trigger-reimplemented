"""Good Night ending, from the End of Time bucket in New Game+.

Save: technoplaza ct-state18 slot 2 (Crono, Robo and Lucca at the End of Time).

The bucket to the Day of Lavos in 1999 A.D., the shell with its boss imitations,
the inner cavern, the inner Lavos and the Lavos Core, then the credits with the
three sleeping monsters.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Good Night ending, from the End of Time bucket in New Game+: load the End of Time save (Crono, Robo, Lucca), examine the
# bucket twice (Gaspar's warning, then the Day of Lavos Yes/No prompt), the Day of Lavos in 1999 A.D. (the walk, Robo's
# Fight!/Run away choice, the Lavos rising cutscene), then Lavos's outer shell with its boss imitations and the shell
# itself, the inner cavern walk, the inner Lavos with its arms and the humanoid form, and the Lavos Core (body, left bit and
# core), all fought with Luminaire, Lightning, Lightning 2, Slash, Flare, Mega Bomb, Fire 2, Fire, Napalm, Uzzi Punch,
# Laser Spin, Area Bomb, Rocket Punch, Heal Beam, attacks, a Revive, Full Tonics, Elixirs, HyperEthers and MegaElixirs.
# Ending: the staff credits with the three sleeping monsters and the "The End zz" screen.
# Save: technoplaza ct-state18 slot 2 (games.technoplaza.net/snes/chronotrigger/savestates.php, Chrono Trigger (U).srm).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/technoplaza/ct-state18.srm"
PHASES = ["eot", "dayoflavos", "shell", "cavern", "inner", "core", "ending"]


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        machine.run(4, "A")
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.checkpoint("eot")
    if start <= 1:
        run.wait_control()
        assert run.go(552, 316, tolerance=5), "the bucket"
        machine.run(2, "UP")
        machine.run(8)
        for _ in range(6):
            machine.run(3, "A")
            machine.run(40)
            if run.dialog():
                break
        run.dialogs_until(lambda: not run.dialog(), 3000)    # Gaspar's warning
        machine.run(30)
        machine.run(3, "A")                                  # examine again: the Yes/No prompt
        machine.run(40)
        assert run.dialogs_until(lambda: run.map_id() == "d201", 8000), "1999 A.D."
        run.checkpoint("dayoflavos")
    if start <= 2:
        run.wait_control()
        assert run.go(236, 720, tolerance=6)
        assert run.go(240, 380, tolerance=6)
        machine.run(100, "UP")                               # walking north starts the cutscene
        assert run.dialogs_until(lambda: run.map_id() == "d301", 20000), "Lavos rises"
        run.checkpoint("shell")
    if start <= 3:
        # Between the shell's boss imitations the party has to walk into Lavos again.
        run.fight("party", lambda r, s: r.map_id() == "da01", party=3, seed=args.seed,
                  idle=lambda bot: bot.step(22, "UP"))
        run.checkpoint("cavern")
    if start <= 4:
        run.wait_control()
        assert run.exit_to("db01"), "the inner cavern's second chamber"
        run.checkpoint("inner")
    if start <= 5:
        run.fight("party", lambda r, s: r.map_id() == "df01", party=3, seed=args.seed)
        run.checkpoint("core")
    if start <= 6:
        def core_beaten(r, state):
            return state.max_hp(9) == 30000 and state.hp(9) == 0 and any(state.hp(slot) for slot in range(3))
        run.fight("party", core_beaten, party=3, seed=args.seed)
        run.checkpoint("ending")
    run.dialogs_until(lambda: False, 17000)                  # the credits
    machine.screenshot(run.prefix + "_end.png")
    run.say("end frame", machine.frame)
    run.finish("the Good Night ending")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
