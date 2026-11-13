"""Leg C of the playthrough: from Tyrano Lair to the Ocean Palace's Lavos.

Save: ctsave08 slot 1 (fantasyanime archive; Crono, Ayla and Marle at Tyrano
Lair's save point, $12F, Azala and the Black Tyrano ahead).

The leg plays the Black Tyrano and Lavos's fall, 12000 B.C. and Zeal, the
Epoch, Terra Cave and the Mountain of Woe, the Ocean Palace and the Golem
Twins, and ends as Lavos rises from the Mammon Machine. Where another route
already plays a stretch from a save, the leg calls its `play(run, start)` from
the state it reached. The party and items are the save's.
"""
from .. import addresses as A
from .. import bfs
from ..campaign import Run, route_arguments, run_route
from ._trial_walk import world_enter
from .ozzies_fort import fly_to, land
from .playthrough import in_hand, leave_fighting, naming, through, win
from . import black_tyrano
from . import zeal_palace

SAVE = "saves/fantasyanime/ctsave08-zeal.srm"
SLOT = 0
STORY = 0x7F0000
LAIR_RUINS = 0x133          # the crater Lavos left, 65,000,000 B.C.
WORLD_65M, LAIR_RUINS_MAP = "f301", "3301"
LAIR_RUINS_GATE = 8         # an object of the Lair Ruins
CAVE_12KBC, SNOWFIELD = "5f01", "f401"
ZEAL_HALL, MAMMON_PASSAGE, SEALED_DOOR_HALL, QUEENS_ROOM = "9101", "4a01", "4901", "4c01"
SEALED_DOOR = (328, 144)    # in Zeal Palace's hall, up to the Queen's door
EXILED = 0xAB               # the story once Schala has sent the party back
TRAVEL_ON_FOOT, TRAVEL_ON_A_DACTYL = 0, 3
MYSTIC_MTS = 0x112
MYSTIC_MTS_FOOT, MYSTIC_MTS_TOP, MYSTIC_MTS_GATE = "1201", "1101", "1001"
MYSTIC_LEDGE = (170, 104)   # above the Gate, where the party jumps down to it
END_OF_TIME = "d001"
GATE_TILE = 0x7F0212        # the leader's tile at the End of Time, column then row two bytes on
PILLAR_2300 = (13, 10)      # the End of Time's pillar of light to Proto Dome
PROTO_DOME_GATE, PROTO_DOME, WORLD_2300 = "e300", "e200", "f201"
LAB32_EAST = 0x0E1
LAB32_EAST_MAP, LAB32, LAB32_WEST = "e100", "e000", "df00"
SEWER_ACCESS = 0x0EC
SEWER, SEWER_DEEP = "ec00", "ed00"
SEWER_DEEP_STAIR = (456, 344)   # $0ED's way up into the sewer's lower half
SEWER_SWITCH = (344, 410)       # below the switch, in the pocket under the shutter
KEEPERS_DOME = 0x0F1
BELTHASARS_HALL, HANGAR = "f200", "f300"
KEEPERS_SEALED_DOOR = (640, 352)
HALL_SEALED_DOOR = (48, 88)
HALL_RECORDS = range(8, 14)     # objects of Belthasar's hall
THE_EPOCH = 13              # an object of the hangar
EPOCH_NOSE = 300            # the hangar's row below the Epoch's nose
EPOCH_DIAL = 0x7E0C52       # the era the Epoch's dial points at
DIAL_12KBC = 5
TERRA_CAVE, TERRA_CAVE_DOOR = 0x182, (424, 472)
ALGETTY = "7c01"
TYRANO = black_tyrano.PHASES[1:]
# Out of the scene onto the 65,000,000 B.C. map, into the Lair Ruins and
# through the Gate there to 12000 B.C., out onto the snowfield's map.
GATE = ["lair_ruins", "gate_12kbc", "snowfield"]
ZEAL = zeal_palace.PHASES[1:]
# Back through the sealed door to the Queen, Dalton's Golem, and Schala's
# spell that sends the party back to the Lair Ruins with the Gate sealed.
EXILE = ["queen_door", "golem", "golem_won", "exiled"]
# A dactyl off the plateau to the Mystic Mountains, whose Gate goes to the
# End of Time; its 2300 A.D. pillar, Proto Dome and Lab 32 to the Sewer
# Access; through it to Keeper's Dome, Belthasar's Epoch, and the Epoch's
# first flight to 12000 B.C.: Algetty, under Terra Cave.
EPOCH = ["dactyl", "mystic_mts", "mystic_gate", "end_of_time", "proto_dome", "lab32", "sewer", "sewer_lower",
         "sewer_switch", "sewer_out", "keepers_dome", "hangar", "belthasar", "epoch_dial", "epoch_12kbc",
         "algetty"]
PHASES = ["loaded"] + TYRANO + GATE + ZEAL + EXILE + EPOCH
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


def stuck_in_hand(run, max_frames=30000, seed=1):
    """in_hand, for a scene that also waits on a press with no window open:
    Belthasar's, before he has the party name the Epoch."""
    machine = run.machine
    start = machine.frame
    while machine.frame - start < max_frames:
        if in_hand(run, 600, seed):
            return True
        if not run.dialog() and not naming(run):
            machine.run(3, "A")
            machine.run(37)
    return False


def onward(run, avoid=frozenset(), rounds=20, seed=1):
    """Walk on to any map but this one and those in `avoid`, winning the
    fights on the way: leave_fighting for a way out not known by name."""
    machine = run.machine
    here = run.map_id()
    away = lambda: run.map_id() not in avoid | {here}
    for _ in range(rounds):
        assert in_hand(run), "the field in hand"
        if away():
            return True
        moves = run.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() not in avoid | {here}
                         or not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN))
        run.say("onward", "steps", moves and len(moves), "now", run.map_id(), "at", run.me())
        if away():
            return True
        if moves is None:
            return False
        machine.run(30)
        assert in_hand(run, 30000, seed), "the fight or scene on the way ends"
    return away()


def walk_fighting(run, goal, seed=1, rounds=12):
    """Walk this map until `goal(x, y)`, winning the fights that stop the walk."""
    machine = run.machine
    for _ in range(rounds):
        assert in_hand(run), "the field in hand"
        if goal(*run.me()):
            return True
        moves = run.walk_here(lambda m, px, py: goal(px, py) or not m.b(A.FIELD_PLAYER_CONTROL)
                              or m.b(A.DIALOG_OPEN))
        if moves is None:
            return False
        if goal(*run.me()):
            return True
        machine.run(30)
        assert in_hand(run, 30000, seed), "the fight or scene on the way ends"
    return False


def exile(run, ahead, seed):
    """From the Mammon Machine's room, the pendant opens the Queen's door:
    Dalton's Golem, and Schala sends the party back to the Lair Ruins."""
    machine = run.machine
    if ahead("queen_door"):
        assert run.exit_to(MAMMON_PASSAGE) and run.exit_to(ZEAL_HALL), "Zeal Palace's hall"
        zeal_palace.doorway(run, *SEALED_DOOR, "UP", SEALED_DOOR_HALL)
        # Up the hall to the Queen's door, which the charged pendant opens.
        for _ in range(80):
            machine.run(8, "UP")
            if not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        assert run.wait_control(), "the door opened"
        for _ in range(60):
            machine.run(8, "UP")
            if run.map_id() != SEALED_DOOR_HALL:
                break
        assert run.map_id() == QUEENS_ROOM and run.wait_control(), "the Queen's room"
        run.checkpoint("queen_door")
    if ahead("golem"):
        for _ in range(80):
            machine.run(8, "UP")
            if run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        assert run.dialogs_until(lambda: A.battle_menu_open(machine), 8000), "Golem's battle"
        run.checkpoint("golem")
    if ahead("golem_won"):
        win(run, seed)
        run.checkpoint("golem_won")
    if ahead("exiled"):
        assert run.dialogs_until(lambda: run.map_id() == LAIR_RUINS_MAP and machine.b(STORY) == EXILED, 20000), \
            "back in the Lair Ruins"
        assert run.wait_control(), "the party in hand"
        run.checkpoint("exiled")


def dactyl_to(run, x, y, location):
    """Board the dactyl that waits by the Lair Ruins, fly it to (x, y), land
    on the nearest open ground and walk into `location`."""
    machine = run.machine
    moves, _ = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT,
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(), confirm=True, max_nodes=800)
    machine.run(60)
    assert moves is not None and machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_ON_A_DACTYL, "aboard the dactyl"
    fly_to(run, x, y)
    assert land(run, max_nodes=200), "ground to land the dactyl on"
    world_enter(run, location)


def to_the_epoch(run, ahead, seed):
    """From the Lair Ruins to Algetty in 12000 B.C., by the End of Time,
    2300 A.D. and Belthasar's Epoch."""
    machine = run.machine
    if ahead("dactyl"):
        assert run.exit_to(WORLD_65M), "out onto the plateau"
        assert run.wait_world_control(), "the map in hand"
        dactyl_to(run, 392, 712, MYSTIC_MTS)
        assert in_hand(run), "the Mystic Mountains in hand"
        run.checkpoint("dactyl")
    if ahead("mystic_mts"):
        through(run, 120, 0, lambda: run.map_id() != MYSTIC_MTS_FOOT, seed=seed, rounds=20)
        assert run.map_id() == MYSTIC_MTS_TOP and in_hand(run), "up the mountain"
        run.checkpoint("mystic_mts")
    if ahead("mystic_gate"):
        assert run.exit_other(frozenset({MYSTIC_MTS_FOOT, WORLD_65M})), "the Gate's slope"
        assert run.map_id() == MYSTIC_MTS_GATE and in_hand(run), "the slope in hand"
        run.checkpoint("mystic_gate")
    if ahead("end_of_time"):
        # Off the ledge above the Gate ("jump down?" answered yes), and in.
        assert run.go(*MYSTIC_LEDGE), "the ledge"
        machine.run(60, "DOWN")
        machine.run(30)
        machine.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == END_OF_TIME, 3000), "the End of Time"
        assert run.wait_control(), "the party at the End of Time"
        run.checkpoint("end_of_time")
    if ahead("proto_dome"):
        assert run.walk(lambda m, x, y: (m.b(GATE_TILE), m.b(GATE_TILE + 2)) == PILLAR_2300) is not None, \
            "2300 A.D.'s pillar"
        # The pillar takes the party once it has settled on it: a step that
        # stops on the pillar's tile can leave it a press short.
        for nudge in ("UP", "DOWN", "UP", "DOWN"):
            machine.run(4, nudge)
            machine.run(3, "A")
            machine.run(60)
            machine.run(3, "A")
            if run.dialogs_until(lambda: run.map_id() == PROTO_DOME_GATE, 900):
                break
        assert run.map_id() == PROTO_DOME_GATE, "Proto Dome's Gate"
        assert run.wait_control()
        assert run.exit_to(PROTO_DOME), "Proto Dome"
        assert leave_fighting(run, WORLD_2300), "the 2300 A.D. map"
        run.checkpoint("proto_dome")
    if ahead("lab32"):
        run.wait_world_control()
        world_enter(run, LAB32_EAST)
        assert in_hand(run)
        assert onward(run, frozenset({WORLD_2300}), seed=seed) and run.map_id() == LAB32, "Lab 32"
        assert onward(run, frozenset({LAB32_EAST_MAP}), seed=seed) and run.map_id() == LAB32_WEST, "its west end"
        assert leave_fighting(run, WORLD_2300), "out by Arris Dome"
        run.checkpoint("lab32")
    if ahead("sewer"):
        run.wait_world_control()
        world_enter(run, SEWER_ACCESS)
        assert onward(run, frozenset({WORLD_2300}), seed=seed) and run.map_id() == SEWER_DEEP, "the fishmen's floor"
        run.checkpoint("sewer")
    if ahead("sewer_lower"):
        # Leave everything on this floor alone; its stair up comes out in
        # the sewer's lower half.
        x, y = SEWER_DEEP_STAIR
        assert walk_fighting(run, lambda px, py: abs(px - x) < 10 and abs(py - y) < 10, seed), "the stair up"
        machine.run(40, "UP")
        machine.run(60)
        assert run.map_id() == SEWER and in_hand(run), "the sewer's lower half"
        run.checkpoint("sewer_lower")
    if ahead("sewer_switch"):
        # The notice on the machine says to go round the back and press the
        # switch: it is in the pocket below the shutter, faced from below.
        x, y = SEWER_SWITCH
        assert walk_fighting(run, lambda px, py: abs(px - x) < 6 and abs(py - y) < 6, seed), "below the switch"
        machine.run(40, "UP")
        machine.run(3, "A")
        machine.run(60)
        assert in_hand(run)
        run.checkpoint("sewer_switch")
    if ahead("sewer_out"):
        assert onward(run, frozenset({SEWER_DEEP}), seed=seed) and run.map_id() == WORLD_2300, "out by Keeper's Dome"
        run.checkpoint("sewer_out")
    if ahead("keepers_dome"):
        run.wait_world_control()
        world_enter(run, KEEPERS_DOME)
        assert in_hand(run)
        # Past the sleeping Nu, the pendant opens the dome's sealed door.
        assert run.go(*KEEPERS_SEALED_DOOR), "the sealed door"
        machine.run(20, "UP")
        machine.run(3, "A")
        assert leave_fighting(run, BELTHASARS_HALL), "Belthasar's hall"
        run.checkpoint("keepers_dome")
    if ahead("hangar"):
        # Belthasar's records glimmer along the hall; the way to its far
        # sealed door is open once the last has been read.
        for record in HALL_RECORDS:
            assert run.talk(record), "a record of Belthasar's"
        assert run.go(*HALL_SEALED_DOOR), "the hall's sealed door"
        machine.run(20, "UP")
        machine.run(3, "A")
        assert leave_fighting(run, HANGAR), "the hangar"
        run.checkpoint("hangar")
    if ahead("belthasar"):
        # Up the corridor, its door opened with confirm, to the Epoch's
        # nose; beside it the party wonders at it; back down, the Nu comes
        # in as Belthasar and has them name it.
        for _ in range(40):
            y = run.me()[1]
            machine.run(20, "UP")
            if run.me()[1] < EPOCH_NOSE:
                break
            if run.me()[1] == y:
                machine.run(3, "A")
                machine.run(40)
                assert in_hand(run), "the corridor in hand"
        assert run.me()[1] < EPOCH_NOSE, "up to the Epoch"
        assert run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN),
                        max_nodes=6000) is not None, "beside the Epoch"
        assert in_hand(run)
        assert run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) or m.b(A.DIALOG_OPEN),
                        max_nodes=6000) is not None, "back down the hangar"
        assert stuck_in_hand(run), "the Epoch named"
        run.checkpoint("belthasar")
    if ahead("epoch_dial"):
        # Confirm beside it asks whether to board; yes opens its dial.
        assert run.talk(THE_EPOCH, done=lambda: run.dialog() or not machine.b(A.FIELD_PLAYER_CONTROL)), "the Epoch"
        assert run.dialogs_until(lambda: run.map_id() == WORLD_2300, 6000), "aboard the Epoch"
        machine.run(600)
        for _ in range(8):
            if machine.b(EPOCH_DIAL) == DIAL_12KBC:
                break
            machine.run(4, "DOWN" if machine.b(EPOCH_DIAL) < DIAL_12KBC else "UP")
            machine.run(56)
        assert machine.b(EPOCH_DIAL) == DIAL_12KBC, "12000 B.C. on the dial"
        run.checkpoint("epoch_dial")
    if ahead("epoch_12kbc"):
        machine.run(3, "A")
        assert run.dialogs_until(lambda: run.map_id() == SNOWFIELD
                                 and machine.b(A.OVERWORLD_TRAVEL) != TRAVEL_ON_FOOT, 8000), "12000 B.C."
        run.checkpoint("epoch_12kbc")
    if ahead("algetty"):
        fly_to(run, *TERRA_CAVE_DOOR)
        assert land(run), "ground by Terra Cave"
        world_enter(run, TERRA_CAVE)
        assert leave_fighting(run, ALGETTY), "Algetty"
        run.checkpoint("algetty")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    try:
        start = run.start_at(PHASES)
        ahead = lambda name: start <= PHASES.index(name)

        def within(phases):
            """Where another route's stretch starts in this run (its `play`'s
            `start`), or None when the run resumes past it."""
            first = PHASES.index(phases[0])
            return max(1, start - first + 1) if start < first + len(phases) else None

        if ahead("loaded"):
            load(run)
            run.checkpoint("loaded")
        if within(TYRANO):
            black_tyrano.play(run, within(TYRANO), args.seed)
        if ahead("lair_ruins"):
            # Ayla's farewell to her people, then the plateau: the crater
            # north of it is the Lair Ruins.
            assert world_in_hand(run, WORLD_65M), "the 65,000,000 B.C. map in hand"
            world_enter(run, LAIR_RUINS)
            assert run.wait_control(), "the party in the Lair Ruins"
            run.checkpoint("lair_ruins")
        if ahead("gate_12kbc"):
            # The Gate the fall opened goes to 12000 B.C.: a cave on the snowfield.
            assert run.talk(LAIR_RUINS_GATE, done=lambda: run.map_id() != LAIR_RUINS_MAP), "the Gate"
            assert run.wait_control(), "the party in the cave"
            assert run.map_id() == CAVE_12KBC and machine.b(STORY) == 0x9F, "12000 B.C."
            run.checkpoint("gate_12kbc")
        if ahead("snowfield"):
            assert run.exit_to(SNOWFIELD), "out onto the snowfield"
            machine.run(120)
            run.checkpoint("snowfield")
        if within(ZEAL):
            zeal_palace.play(run, within(ZEAL))
        exile(run, ahead, args.seed)
        to_the_epoch(run, ahead, args.seed)
        run.finish("the leg's last checkpoint")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if not run.logfile.closed:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
