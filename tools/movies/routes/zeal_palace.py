"""Meet Schala in Zeal Palace, watch her open the sealed door and charge the pendant.

Ctsave08 slot 2 is in the snowfield at story $9F. Slot 3 is already at $AB,
after the palace and exile. The route stops at $A5 with the Mammon Machine
room back in hand, before entering the throne room.
"""
from ..campaign import Run, route_arguments, run_route
from ._zeal_navigation import approach

SAVE = "oracle/saves/fantasyanime/ctsave08-zeal.srm"
PHASES = ["loaded", "island", "snow2", "mainland", "mountain", "palace", "schala", "door", "charged"]
HEADER = """\
# Zeal Palace: the Skyways and mountain passages, Schala and Janus, the sealed door,
# and charging the pendant at the Mammon Machine, until the field returns (route zeal_palace).
# Save: ctsave08 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def enter(run, x, y, destination):
    approach(run, x, y, world=True)
    run.machine.run(4, "A")
    run.machine.run(160)
    assert run.map_id() == destination, (destination, run.map_id(), run.world_pos())
    assert run.wait_control()


def beam(run, frames, destination):
    run.machine.run(frames, "UP")
    run.machine.run(400)
    assert run.map_id() == destination
    assert run.wait_control()


def outside(run, destination):
    assert run.exit_to(destination)
    run.machine.run(120)


def doorway(run, x, y, direction, destination):
    approach(run, x, y)
    run.machine.run(64, direction)
    run.machine.run(90)
    assert run.map_id() == destination
    assert run.wait_control(12000)


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)
    try:
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(1)
            machine.run(4, "A")
            machine.run(300)
            assert run.map_id() == "f401" and machine.b(0x7F0000) == 0x9F
            assert not machine.b(0x7F00F4) & 0x10, "Schala's meeting still ahead"
            run.checkpoint("loaded")
        if start <= 1:
            enter(run, 864, 688, "5101")
            beam(run, 90, "6101")
            outside(run, "f501")
            run.checkpoint("island")
        if start <= 2:
            enter(run, 936, 600, "5001")
            beam(run, 180, "6201")
            outside(run, "f401")
            run.checkpoint("snow2")
        if start <= 3:
            enter(run, 992, 192, "6401")
            beam(run, 130, "6001")
            outside(run, "f501")
            run.checkpoint("mainland")
        if start <= 4:
            enter(run, 728, 496, "8501")
            beam(run, 140, "8501")
            assert run.me()[0] > 400
            outside(run, "f501")
            enter(run, 816, 456, "8501")
            beam(run, 140, "8501")
            assert run.me()[0] > 800
            outside(run, "f501")
            run.checkpoint("mountain")
        if start <= 5:
            enter(run, 808, 400, "9101")
            run.checkpoint("palace")
        if start <= 6:
            doorway(run, 584, 176, "UP", "9201")
            doorway(run, 968, 112, "UP", "4801")
            assert machine.b(0x7F00F4) & 0x10
            run.checkpoint("schala")
        if start <= 7:
            doorway(run, 824, 448, "DOWN", "9201")
            doorway(run, 888, 176, "DOWN", "9101")
            doorway(run, 328, 144, "UP", "4901")
            machine.run(280, "UP")
            assert run.dialogs_until(lambda: machine.b(0x7F0000) == 0xA2, 8000)
            assert run.wait_control()
            run.checkpoint("door")
        if start <= 8:
            doorway(run, 120, 464, "DOWN", "9101")
            doorway(run, 136, 224, "UP", "4a01")
            # The platform changes maps before a walking search can stop on
            # its destination tile. Release UP as soon as that change starts.
            for _ in range(100):
                machine.run(8, "UP")
                if run.map_id() == "4b01":
                    break
            assert run.map_id() == "4b01"
            machine.run(160)
            assert run.wait_control()
            machine.run(100, "UP")
            machine.run(3, "A")
            assert run.dialogs_until(lambda: machine.b(0x7F0000) == 0xA5, 8000)
            assert run.wait_control()
            assert run.map_id() == "4b01" and machine.b(0x7F0000) == 0xA5
            run.checkpoint("charged")
        run.finish("Schala's sealed door opened and the pendant charged at the Mammon Machine")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
