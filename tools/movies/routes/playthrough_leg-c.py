"""Leg C of the playthrough: from Tyrano Lair to the Ocean Palace's Lavos.

Save: ctsave08 slot 1 (fantasyanime archive; Crono, Ayla and Marle at Tyrano
Lair's save point, $12F, Azala and the Black Tyrano ahead).

The leg plays the Black Tyrano and Lavos's fall, 12000 B.C. and Zeal, the
Epoch, Terra Cave and the Mountain of Woe, the Ocean Palace and the Golem
Twins, and ends as Lavos rises from the Mammon Machine. Where another route
already plays a stretch from a save, the leg calls its `play(run, start)` from
the state it reached. The party and items are the save's.
"""
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import world_enter
from . import black_tyrano
from . import zeal_palace

SAVE = "saves/fantasyanime/ctsave08-zeal.srm"
SLOT = 0
STORY = 0x7F0000
LAIR_RUINS = 0x133          # the crater Lavos left, 65,000,000 B.C.
WORLD_65M, LAIR_RUINS_MAP = "f301", "3301"
LAIR_RUINS_GATE = 8         # an object of the Lair Ruins
CAVE_12KBC, SNOWFIELD = "5f01", "f401"
TYRANO = black_tyrano.PHASES[1:]
# Out of the scene onto the 65,000,000 B.C. map, into the Lair Ruins and
# through the Gate there to 12000 B.C., out onto the snowfield's map.
GATE = ["lair_ruins", "gate_12kbc", "snowfield"]
ZEAL = zeal_palace.PHASES[1:]
PHASES = ["loaded"] + TYRANO + GATE + ZEAL
assert len(set(PHASES)) == len(PHASES), "a checkpoint name each phase"
HEADER = """\
# Leg C of the playthrough (route playthrough_leg-c): from Tyrano Lair to the Ocean Palace's Lavos.
# Save: ctsave08 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def load(run):
    """Power on and load the save's slot, until the party is in hand."""
    machine = run.machine
    machine.run(1620)
    machine.run(5, "START")
    machine.run(2420 - machine.frame)
    assert run.select_slot(SLOT)
    machine.run(4, "A")
    for _ in range(200):
        machine.run(10)
        if run.map_id() not in ("0000", "b101"):
            break
    assert run.wait_control(), "the party in hand"
    assert run.map_id() == black_tyrano.SAVE_ROOM, "Tyrano Lair's save point"


def world_in_hand(run, world, max_frames=20000):
    """Close windows until the party walks world map `world`. The scene after
    the Black Tyrano shows the map, leaves it for Ayla's people and comes back."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        if run.dialog():
            machine.run(3, "A")
            machine.run(40)
        elif run.map_id() == world and run.wait_world_control(30):
            return True
        else:
            machine.run(10)
    return False


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            load(run)
            run.checkpoint("loaded")
        if start < 1 + len(TYRANO):
            black_tyrano.play(run, max(1, start), args.seed)
        later = start - 1 - len(TYRANO)
        if later <= 0:
            # Ayla's farewell to her people, then the plateau: the crater
            # north of it is the Lair Ruins.
            assert world_in_hand(run, WORLD_65M), "the 65,000,000 B.C. map in hand"
            world_enter(run, LAIR_RUINS)
            assert run.wait_control(), "the party in the Lair Ruins"
            run.checkpoint("lair_ruins")
        if later <= 1:
            # The Gate the fall opened goes to 12000 B.C.: a cave on the snowfield.
            assert run.talk(LAIR_RUINS_GATE, done=lambda: run.map_id() != LAIR_RUINS_MAP), "the Gate"
            assert run.wait_control(), "the party in the cave"
            assert run.map_id() == CAVE_12KBC and machine.b(STORY) == 0x9F, "12000 B.C."
            run.checkpoint("gate_12kbc")
        if later <= 2:
            assert run.exit_to(SNOWFIELD), "out onto the snowfield"
            run.machine.run(120)
            run.checkpoint("snowfield")
        if later < len(GATE) + len(ZEAL):
            zeal_palace.play(run, max(1, later - len(GATE) + 1))
        later -= len(GATE) + len(ZEAL)
        run.finish("the leg's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
