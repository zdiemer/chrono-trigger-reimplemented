"""Beat both rounds of Slash in Magus's castle, then return to field control."""
from ..campaign import route_arguments, run_route
from ..castle import SAVE, APPROACH, approach, CastleRun, FLAGS, MENU_MEMBER, castle_policy

PHASES = APPROACH + ["slash_hall", "slash_room", "henchmen", "slash", "won", "after"]
HEADER = """\
# Magus's castle, 600 A.D.: explore the wings, Ozzie's ambush, Slash's henchmen,
# then Slash unarmed and with his sword, ending with the field back in hand (route castle_slash).
# Save: ctsave06 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = CastleRun(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    try:
        start = run.start_at(PHASES)
        approach(run, start, args.seed)
        m = run.machine
        if start <= 15:
            assert run.exit_to("d501")
            assert run.wait_control()
            run.checkpoint("slash_hall")
        if start <= 16:
            assert run.exit_to("a900")
            assert run.wait_control()
            run.checkpoint("slash_room")
        if start <= 17:
            assert run.talk(9), "the henchmen before Slash"
            run.fight(castle_policy, lambda r, s: bool(m.b(FLAGS) & 0x40),
                      seed=args.seed, max_frames=25000)
            assert run.wait_control()
            run.checkpoint("henchmen")
        if start <= 18:
            assert run.talk(11), "Slash"
            assert run.dialogs_until(lambda: m.b(MENU_MEMBER) < 3, 8000)
            run.checkpoint("slash")
        if start <= 19:
            run.fight(castle_policy, lambda r, s: bool(m.b(FLAGS) & 4),
                      seed=args.seed, max_frames=30000)
            run.checkpoint("won")
        if start <= 20:
            assert run.wait_control(8000), "Slash beaten, field back in hand"
            assert run.map_id() == "a900" and m.b(FLAGS) & 4
            run.checkpoint("after")
        run.finish("both rounds of Slash beaten, field in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
