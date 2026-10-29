"""A fight in the Denadoro Mountains, 600 A.D.

Save: ctsave04 slot 2 (fantasyanime archive; the party on the 600 A.D. world map beside the
mountains, $1F1).

The way: from the world map into the mountains ($091), then up to the monsters standing about
nearest the party until one attacks, and the battle bot wins the fight. The mountains' monsters
are fought in no other movie. The route stops once the field is back in hand.
"""
from ..campaign import Run, route_arguments, run_route
from ..encounter import enter_from_the_world_map, load_slot, walk_into_a_fight_from, win_the_fight

HEADER = """\
# 600 A.D.: from the world map into the Denadoro Mountains ($091), up to the monsters nearest the party until one
# attacks; the battle bot wins the fight, then the field back in hand (route denadoro_fight).
# Save: ctsave04 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave04-masamune.srm"
SLOT = 1
PHASES = ["loaded", "inside", "battle", "won", "after"]


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        load_slot(run, SLOT)
        run.checkpoint("loaded")
    if start <= 1:
        assert enter_from_the_world_map(run), "into the mountains"
        run.checkpoint("inside")
    if start <= 2:
        assert walk_into_a_fight_from(run, "inside"), "a fight"
        run.checkpoint("battle")
    if start <= 3:
        win_the_fight(run, args.seed)
        run.checkpoint("won")
    if start <= 4:
        assert run.wait_control(6000), "the field back in hand"
        run.checkpoint("after")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("a Denadoro fight won")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
