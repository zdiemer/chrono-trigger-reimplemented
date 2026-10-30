"""Beat Death Peak's final Lavos Spawn and use the Time Egg to bring Crono back.

Start at the cartridge save made by death_peak. Push the shell to tile
(16, 15), climb it, use the Chrono Trigger at the summit, and exchange the
clone for Crono in the frozen Ocean Palace.
"""
from ..campaign import Run, route_arguments, run_route
from .death_peak import arrive, spawn, walk_to

SAVE = "oracle/saves/routes/death-peak-summit.srm"
PHASES = ["loaded", "spawn", "shell", "climbed", "summit", "frozen", "returned"]
HEADER = """\
# Death Peak, 2300 A.D.: beat the final Lavos Spawn, push its shell into place and climb to the summit.
# Use the Time Egg, exchange the clone for Crono in the frozen Ocean Palace, and return to field control.
# Save: death-peak-summit slot 1 (oracle/saves/routes), made by events-death-peak-ascent; route death_peak_return.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(0)
            m.run(4, "A")
            m.run(500)
            assert run.find_player() is not None
            assert run.map_id() == "ef01" and m.b(0x7F0064) & 0x38 == 0x18
            assert all(item in m.peek(0x7E2400, 242) for item in (0xD9, 0xE2))
            run.checkpoint("loaded")
        else:
            assert run.find_player() is not None
        if start <= 1:
            walk_to(run, 72, 358)
            m.run(20, "UP+B")
            spawn(run, 32, args.seed)
            run.checkpoint("spawn")
        if start <= 2:
            walk_to(run, 120, 180)
            walk_to(run, 148, 277, limit=500)
            for _ in range(400):
                m.run(1, "RIGHT")
                if run.object_pos(9)[0] >= 256:
                    break
            m.run(60)
            assert m.b(0x7F0065) == 16, "shell in the ladder's column"
            sx, sy = run.object_pos(9)
            walk_to(run, sx, sy + 40, limit=400)
            m.run(180, "UP")
            assert m.peek(0x7F0065, 2) == bytes((16, 15)), "shell under the ladder"
            run.checkpoint("shell")
        if start <= 3:
            m.run(30, "A")
            # At the top of the shell only UP is possible. find_player needs
            # opposite directions, so first walk out of that narrow landing.
            for _ in range(400):
                m.run(2, "UP+B")
                if run.me()[1] < 160:
                    break
            assert run.me()[1] < 160 and run.wait_control(), "off the shell's narrow landing"
            run.checkpoint("climbed")
        if start <= 4:
            arrive(run, 264, 16, "0901")
            run.checkpoint("summit")
        if start <= 5:
            m.run(130, "UP+B")
            assert run.dialogs_until(lambda: run.map_id() == "a701", 16000), "time frozen in the Ocean Palace"
            assert run.wait_control(8000), "the frozen palace in hand"
            run.checkpoint("frozen")
        if start <= 6:
            x, y = run.object_pos(10)
            walk_to(run, x, y + 16)
            m.run(12, "UP")
            m.run(30, "A")
            m.run(120)
            assert run.dialogs_until(lambda: run.map_id() == "d001" and m.b(0x7F0057) & 64, 16000), "Crono returned"
            assert run.wait_control(8000), "the field after Crono's return"
            run.checkpoint("returned")
        assert m.b(0x7F0057) & 64, "Crono restored"
        assert all(item not in m.peek(0x7E2400, 242) for item in (0xD9, 0xE2)), "Time Egg and clone consumed"
        run.finish("Crono returned and the field back in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
