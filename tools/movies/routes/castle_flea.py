"""Beat the decoy and Flea in Magus's castle, then return to field control."""
from ..campaign import route_arguments, run_route
from ..castle import SAVE, APPROACH, approach, CastleRun, FLAGS, castle_policy

PHASES = APPROACH + ["flea_hall", "flea_corridor", "flea_room", "flea", "won", "after"]
HEADER = """\
# Magus's castle, 600 A.D.: explore the wings, Ozzie's ambush, and the right wing,
# then the Flea decoy and Flea, ending with the field back in hand (route castle_flea).
# Save: ctsave06 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = CastleRun(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
                    generated=args.generated, until=args.until)
    try:
        start = run.start_at(PHASES)
        approach(run, start, args.seed)
        m = run.machine
        for index, (phase, location) in enumerate(
                [("flea_hall", "ab00"), ("flea_corridor", "7d00"), ("flea_room", "ad00")], 15):
            if start <= index:
                assert run.exit_to(location), phase
                assert run.wait_control()
                run.checkpoint(phase)
        if start <= 18:
            assert run.talk(11), "Flea's decoy"
            run.checkpoint("flea")
        if start <= 19:
            # Flea's 4,120 HP against this party's attacks and Mid Tonics is a
            # long fight: 25,000 frames in the shipping mode and past 30,000 in
            # the native-shape build, with the same blows and another run of
            # random numbers (Flea at 4 HP and 62 HP as the tonics run out).
            # The limit catches a fight that stalls, not one that is slow.
            run.fight(castle_policy, lambda r, s: bool(m.b(FLAGS) & 8),
                      seed=args.seed, max_frames=45000)
            run.checkpoint("won")
        if start <= 20:
            assert run.wait_control(8000), "Flea beaten, field back in hand"
            assert run.map_id() == "ad00" and m.b(FLAGS) & 8
            run.checkpoint("after")
        run.finish("Flea beaten, field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
