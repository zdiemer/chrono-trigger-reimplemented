"""Save Lara in 990 A.D., then return to Robo and receive the GreenDream.

The prepared cartridge save is outside Fiona's shrine, before recovering Robo.
prepare_luccas_mother plays its prerequisites from technoplaza ct-state11.
ctsave13 already has Lara saved; ctsave12 has not even opened the desert.
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route
from .prepare_luccas_mother import boot, dash_to, world_search

SAVE = "oracle/saves/routes/luccas-mother-before-campfire.srm"
PHASES = ["loaded", "shrine", "campfire", "gate", "bedroom", "front", "running",
          "console", "saved_lara", "returned", "after"]
LARA_FLAGS, FOREST_FLAGS = 0x7F007C, 0x7F019E
LARA_SAVED, MACHINE_RUNNING = 0x80, 0x20

HEADER = """\
# Lucca's mother: recover Robo at Fiona's shrine, the campfire, the red gate to 990 A.D., and the accident.
# Activate the machine and enter L, A, R, A against the clock; Lara is saved. Return through the gate, receive
# Robo's GreenDream, and end on the 1000 A.D. world map with control back (route luccas_mother).
# Save: luccas-mother-before-campfire slot 1 (oracle/saves/routes; prepare_luccas_mother from technoplaza ct-state11).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def enter_password(run):
    """Retry the timed letters on copies; only the successful inputs survive.

    The console checks held buttons between waits. A three-frame tap can fall
    entirely between its turns. Lara's saved flag, rather than the console's
    sound or a closed text box, proves that all four letters were accepted.
    """
    m = run.machine
    for _ in range(12):
        if not run.dialog():
            break
        m.press("A", 3, 12)
    assert not run.dialog(), "the password prompt closed"
    m.mark(4002)
    try:
        for hold, gap in ((3, 8), (3, 16), (8, 24), (6, 24), (12, 24), (8, 32), (12, 32)):
            m.restore(4002)
            m.run(4)
            for button in ("L", "A", "R", "A"):
                m.press(button, hold, gap)
            saved = run.dialogs_until(lambda: bool(m.b(LARA_FLAGS) & LARA_SAVED),
                                      1500, press_every=30)
            run.say("password hold", hold, "gap", gap, "saved Lara", saved)
            if saved:
                return
        raise AssertionError("no password timing saved Lara")
    finally:
        m.free(4002)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            boot(run)
            assert run.map_id() == "f001"
            assert m.b(FOREST_FLAGS) & 0x0B == 9, "Robo restoring the forest, not recovered"
            assert not m.b(LARA_FLAGS) & 0x90, "campfire and rescue still unplayed"
            run.checkpoint("loaded")
        if start <= 1:
            world_search(run, lambda m, x, y: run.map_id() == "3900")
            assert run.wait_control(), "Fiona's shrine"
            run.checkpoint("shrine")
        if start <= 2:
            # Robo's altar blocks the generic talk helper's closest spots.
            # Approach its front until the party stands against it.
            assert run.find_player() is not None
            for _ in range(40):
                if run.me()[1] <= 141:
                    break
                m.run(8, "UP+B")
            assert 884 <= run.me()[0] <= 892 and run.me()[1] <= 141
            m.press("A", 3, 120)
            assert run.dialogs_until(lambda: run.map_id() == "b401" and m.b(0x7F006D) & 1, 16000)
            assert run.wait_control(), "Lucca awake after the campfire"
            run.checkpoint("campfire")
        if start <= 3:
            assert run.exit_to("5300") and run.wait_control()
            run.checkpoint("gate")
        if start <= 4:
            assert run.find_player() is not None and run.go(136, 130, 4)
            m.press("UP", 2, 8)
            m.press("A", 3, 30)
            assert run.dialogs_until(lambda: run.map_id() == "0a00", 5000)
            assert run.wait_control(), "Lucca's bedroom in 990 A.D."
            run.checkpoint("bedroom")
        if start <= 5:
            # The bedroom's other exit reaches an isolated balcony. Go through
            # the front room to enter the workshop beside the machine.
            assert run.exit_to("0300") and run.wait_control()
            run.checkpoint("front")
        if start <= 6:
            assert run.exit_to("0400")
            assert run.dialogs_until(lambda: bool(m.b(LARA_FLAGS) & MACHINE_RUNNING), 6000)
            m.run(32)
            run.checkpoint("running")
        if start <= 7:
            run.player = 3  # this workshop's Lucca object
            dash_to(run, (132, 240), lambda m, x, y: abs(x - 132) <= 4 and abs(y - 240) <= 5,
                    max_nodes=1800)
            m.press("RIGHT", 2, 8)
            m.press("A", 3, 30)
            assert run.dialog() and m.b(LARA_FLAGS) & MACHINE_RUNNING, "the timed password prompt"
            run.checkpoint("console")
        if start <= 8:
            enter_password(run)
            assert run.map_id() == "0a00" and m.b(LARA_FLAGS) & LARA_SAVED
            assert run.wait_control(), "Lucca after saving Lara"
            run.checkpoint("saved_lara")
        if start <= 9:
            assert run.find_player() is not None and run.go(376, 106, 4)
            m.press("UP", 2, 8)
            m.press("A", 3, 30)
            assert run.dialogs_until(lambda: run.map_id() == "5300", 6000)
            assert run.wait_control(), "back beside Robo"
            run.checkpoint("returned")
        if start <= 10:
            assert run.talk(2), "Robo's conversation after the rescue"
            assert run.dialogs_until(lambda: run.map_id() == "f001", 6000)
            m.run(180)
            assert m.b(LARA_FLAGS) & LARA_SAVED and m.b(0x7F006A) & 0x10
            assert 0xAA in m.peek(0x7E2400, 0xF2), "the GreenDream received"
            # Prove that the world map is back in hand, retaining the original
            # endpoint rather than the control probes in the movie.
            m.mark(4003)
            before = run.world_pos()
            m.probe(20, "DOWN")
            moved = run.world_pos() != before
            m.restore(4003)
            m.free(4003)
            assert moved, "the field back in hand"
            run.checkpoint("after")
        m.screenshot(run.prefix + "_end.png")
        run.finish("Lara saved and Robo's GreenDream received")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
