"""The Day of Lavos as a game over: the telepod's Lavos fight lost, at the Millennial Fair in New Game+.

Save: technoplaza ct-state16 slot 2 (Crono and Marle at the fair).

The way in is telepod_lavos's (Leene Square, the telepods, the Gate to Lavos's
shell); then nobody acts in the shell's fight, and its first imitation wipes the
party. A loss to Lavos plays the Day of Lavos instead of the game-over screen:
Lavos rising out of the 1999 A.D. world map, the operators' room under the dome,
the 1000 A.D. world map tilted and turned in Mode 7 while the needles fall (the
menus' object scripts' routines at $C6:E7FA-$C6:EE22, which no other movie's
reference play reaches), and the globe with "But... the future refused to
change", where the movie stops: the globe turns until the console is reset.
"""
from ..campaign import Run, route_arguments, run_route
from .telepod_lavos import SAVE, to_the_shell

HEADER = """\
# The Day of Lavos as a game over, from the telepod at the Millennial Fair in New Game+: load the fair save (Crono and
# Marle), walk Leene Square to the telepods, Taban's speech, the right telepod's Gate, then Lavos's outer shell with nobody
# acting until its first imitation wipes the party. The loss to Lavos: Lavos rising out of the 1999 A.D. world map, the
# operators' room (its dialogue pressed through), the 1000 A.D. world map tilted and turned in Mode 7 under the falling
# needles, and the globe turning behind "But... the future refused to change" (route lavos_lost).
# Save: technoplaza ct-state16 slot 2 (games.technoplaza.net/snes/chronotrigger/savestates.php, Chrono Trigger (U).srm).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

PHASES = ["fair", "exhibit", "shell", "lost", "world"]
SHELL, WORLD_1999, WORLD_1000 = "d301", "f701", "f001"
QUIET_FRAMES = 600      # no window for this long: the operators' room is over
GLOBE_FRAMES = 1500     # the globe's scene set up and a few turns of it


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    to_the_shell(run, start)
    if start <= 3:
        # Nobody acts: the battle's menus stay open and the shell's attacks land.
        for _ in range(1000):
            machine.run(30)
            if run.map_id() != SHELL:
                break
        assert run.map_id() == WORLD_1999, "the wipe goes to the 1999 A.D. world map, not %s" % run.map_id()
        run.checkpoint("lost")
    if start <= 4:
        assert run.dialogs_until(lambda: run.map_id() == WORLD_1000, 6000), "the 1000 A.D. world map"
        assert run.dialogs_until(lambda: run.map_id() != WORLD_1000, 3000), "the 1000 A.D. world map's end"
        run.checkpoint("world")
    run.close_dialogs(max_frames=4000, quiet_frames=QUIET_FRAMES)    # the operators' room, to its end
    machine.run(GLOBE_FRAMES)
    machine.screenshot(run.prefix + "_end.png")
    run.finish("the Day of Lavos after a loss")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
