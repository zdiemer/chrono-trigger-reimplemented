"""Play the Ocean Palace finale from the cartridge save made by golem_twins."""
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/recorded/ocean-palace-after-twins.srm"
HEADER = """\
# Ocean Palace after the Golem Twins: reach the Mammon Machine and play Lavos's scripted battle,
# Crono's fall and Zeal's destruction, ending with the party in hand in the Last Village.
# Route: ocean_palace_lavos.
# Save: ocean-palace-after-twins slot 3 (oracle/saves/recorded; made by route golem_twins from ctsave10).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""
PHASES = ["loaded", "after_twins", "mammon", "lavos", "fallen", "crono", "zeal", "village", "after"]
PLAYER_CONTROL = 0x7E011F
MENU_SCREEN = 0x7E0067
STORY = 0x7F0000


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(2)
            machine.run(4, "A")
            machine.run(700)
            assert run.wait_control(), "the party at the save point"
            run.say("loaded", run.map_id(), run.me())
            run.checkpoint("loaded")
        if start <= 1:
            assert run.exit_to("9e01"), "the Twins' empty room"
            assert run.wait_control(), "the party through the cleared room"
            run.checkpoint("after_twins")
        if start <= 2:
            assert run.exit_to("9f01"), "the Mammon Machine's room"
            assert run.wait_control(), "the party before the machine"
            run.say("Mammon", run.me())
            run.checkpoint("mammon")
        if start <= 3:
            # The Nu in the narrow entrance must be spoken to before it steps aside.
            assert run.go(128, 597), "the Nu in the corridor"
            machine.run(2, "UP")
            machine.run(8)
            machine.run(3, "A")
            assert run.wait_control(), "the Nu steps aside"
            assert run.walk(lambda m, x, y: run.map_id() == "9f01" and not m.b(PLAYER_CONTROL)) is not None, \
                "approach the machine"
            run.say("approached", run.me(), "story", machine.b(STORY))
            assert run.dialogs_until(lambda: run.map_id() == "d301", 12000), "Lavos rises"
            run.checkpoint("lavos")
        if start <= 4:
            assert run.dialogs_until(lambda: run.map_id() == "a701", 12000), "the scripted loss to Lavos"
            assert run.wait_control(12000), "Crono's last walk"
            run.say("Crono", run.me(), "story", machine.b(STORY))
            run.checkpoint("fallen")
        if start <= 5:
            assert run.walk(lambda m, x, y: run.map_id() == "a701" and not m.b(PLAYER_CONTROL)) is not None, \
                "Crono approaches Lavos"
            run.checkpoint("crono")
        if start <= 6:
            assert run.dialogs_until(lambda: run.map_id() == "f401", 16000), "Zeal's fall begins"
            run.checkpoint("zeal")
        if start <= 7:
            assert run.dialogs_until(lambda: run.map_id() == "a901", 16000), "the Last Village"
            run.checkpoint("village")
        if start <= 8:
            assert run.dialogs_until(lambda: machine.b(STORY) == 0xCD, 10000), "the party wakes"
            # Crono's absence opens the party exchange. Accept the offered party
            # (Marle, Lucca, Robo), then let the hut reload and return control.
            for _ in range(200):
                if machine.b(MENU_SCREEN) == 7:
                    break
                machine.run(10)
            machine.run(150)
            assert machine.b(MENU_SCREEN) == 7, "the party exchange after Crono's loss"
            for _ in range(4):
                if machine.b(PLAYER_CONTROL):
                    break
                machine.run(3, "B")
                machine.run(90)
            assert run.wait_control(6000), "the field back in hand in the Last Village"
            assert run.map_id() == "a901", "the Last Village hut"
            run.checkpoint("after")
        machine.screenshot(run.prefix + "_end.png")
        run.finish("the Ocean Palace's fall and the Last Village")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
