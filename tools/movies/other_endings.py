"""Shared Lavos fights for the hand-run New Game+ ending routes.

These routes keep their movies in scratch storage: each fight and ending is
too long to replay in every gate. Checkpoints contain only the winning input.
"""
from .campaign import Run, route_arguments
from .routes.telepod_lavos import to_the_shell


TELEPOD_PHASES = ["fair", "exhibit", "shell", "cavern", "inner", "core", "ending"]
BUCKET_PHASES = ["eot", "dayoflavos", "shell", "cavern", "inner", "core", "ending"]


def bucket_to_shell(run, start):
    """Load an End of Time save, examine the bucket, and choose to fight."""
    machine = run.machine
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
        assert run.map_id() == "d001", "the End of Time save"
        run.checkpoint("eot")
    if start <= 1:
        assert run.wait_control()
        assert run.go(552, 316, tolerance=5), "the bucket"
        machine.run(2, "UP")
        machine.run(8)
        for _ in range(6):
            machine.run(3, "A")
            machine.run(40)
            if run.dialog():
                break
        run.dialogs_until(lambda: not run.dialog(), 3000)
        machine.run(30)
        machine.run(3, "A")
        machine.run(40)
        assert run.dialogs_until(lambda: run.map_id() == "d201", 8000), "1999 A.D."
        run.checkpoint("dayoflavos")
    if start <= 2:
        assert run.wait_control()
        assert run.go(236, 720, tolerance=6)
        assert run.go(240, 380, tolerance=6)
        machine.run(100, "UP")
        assert run.dialogs_until(lambda: run.map_id() == "d301", 20000), "Lavos rises"
        run.checkpoint("shell")


def watch_ending(run, *, selected, done, max_frames=48000, settle_frames=120):
    """Watch an automatic ending, proving both its selection and its finish."""
    machine = run.machine
    start, seen, selection_seen = machine.frame, [], False
    scene_flags = machine.b(0x7E0384)
    run.say("ending initial scene flags", "%02X" % scene_flags, "frame", machine.frame)
    while machine.frame - start < max_frames:
        flags = machine.b(0x7E0384)
        if flags != scene_flags:
            run.say("ending scene flags", "%02X -> %02X" % (scene_flags, flags), "frame", machine.frame)
            scene_flags = flags
        here = run.map_id()
        if not seen or seen[-1] != here:
            seen.append(here)
            run.say("ending map", here, "frame", machine.frame)
            machine.screenshot("%s_ending_%d.png" % (run.prefix, machine.frame))
        selection_seen |= bool(selected())
        if selection_seen and done():
            machine.run(settle_frames)
            assert done(), "the ending remains at its finish"
            run.say("ending maps", " ".join(seen))
            return
        if run.dialog():
            machine.run(3, "A")
            machine.run(37)
        else:
            machine.run(20)
    machine.save_state(run.prefix + "_unfinished_ending.st")
    machine.screenshot(run.prefix + "_unfinished_ending.png")
    run.say("ending object positions in script", machine.peek(0x7E1180, 64).hex())
    raise AssertionError("ending did not finish: maps %s, selection seen %s" % (seen, selection_seen))


def lavos(run, start, *, party, seed, core_seed_offset=0, core_policy=None):
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
        run.fight(core_policy or ("crono_and_marle_core" if party == 2 else "party"), core_beaten,
                  party=party, seed=seed + core_seed_offset)
        run.checkpoint("ending")


def play_ending(*, save, title, ending, entry="telepod", party=2, story=None,
                core_seed_offset=0, core_policy=None):
    args = route_arguments(title, save=save)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(TELEPOD_PHASES if entry == "telepod" else BUCKET_PHASES)
        (to_the_shell if entry == "telepod" else bucket_to_shell)(run, start)
        if story is not None and start <= 6:
            # Map loading temporarily borrows bank $7F before restoring flags.
            assert run.dialogs_until(lambda: run.machine.b(0x7F0000) == story, 2000), \
                "the save's story counter"
        lavos(run, start, party=party, seed=args.seed, core_seed_offset=core_seed_offset,
              core_policy=core_policy)
        ending(run)
        run.machine.screenshot(run.prefix + "_end.png")
        if args.movie:
            run.write_movie(args.movie, [
                "# " + title + ": New Game+ " + entry + ", Lavos shell, inner Lavos and Core, then the ending.",
                "# Save: " + save + " slot 2.",
                "# frames {FRAMES}", "# sram crc32 {CRC}",
                "# <start_frame> <frame_count> <buttons>"])
        run.finish(title)
    finally:
        # --until closes the run itself before raising RouteDone.
        if not run.logfile.closed:
            run.close()
