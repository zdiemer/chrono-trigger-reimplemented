"""The winged Epoch on the world maps: boarding it, its time dial, flying it
and coming down beside a place, for routes that travel the late game
(playthrough_leg-d, black_omen_entry)."""
from .. import addresses as A
from .. import bfs
from ._trial_walk import world_enter

TRAVEL_IN_THE_EPOCH = 2
DIAL_PERIOD = 0x7E0C52          # the period the time dial stands at (menu/epoch.c)
PERIOD_END_OF_TIME, PERIOD_2300, PERIOD_1000 = 0, 1, 3
CHOICE_ACTIVE, CHOICE_ROW = 0x7E0162, 0x7E0163
CHOICE_READY = 2                # the choice takes the pad
FULL_SPEED, DISEMBARK = 0, 1    # the docked Epoch's choice
WORLD_EXITS, WORLD_EXIT_BYTES, WORLD_EXIT_COUNT = 0x7E7E00, 7, 128   # the map's exit records
END_OF_TIME_DOCK = "d901"      # the End of Time with the Epoch docked
# Where to try to come down about an entrance, nearest first: the Epoch comes
# down only on open ground, which beside a town is often a few tiles off.
LANDING_SPOTS = sorted(((dx, dy) for dx in range(-96, 97, 16) for dy in range(-96, 97, 16)),
                       key=lambda d: abs(d[0]) + abs(d[1]))
LANDING_SLOT = 3990


def wait_until(machine, condition, frames, step=10):
    """Run without pressing anything until `condition()`, for at most `frames`."""
    for _ in range(frames // step):
        if condition():
            return True
        machine.run(step)
    return condition()


def board(run, settle=True):
    """On a world map on foot: walk up to the Epoch and board it; with
    `settle`, until it has risen and takes the pad."""
    machine = run.machine
    run.wait_world_control()
    moves, _ = bfs.ow_bfs(machine, lambda m, px, py: m.b(A.OVERWORLD_TRAVEL) != 0,
                          lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), run._map(), confirm=True, max_nodes=3000)
    assert moves is not None, "the Epoch to board"
    for _ in range(60):
        machine.run(10)
        if machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH:
            break
    assert machine.b(A.OVERWORLD_TRAVEL) == TRAVEL_IN_THE_EPOCH, "aboard the Epoch"
    # With its wings it rises first, and takes the pad once in the air.
    if settle:
        machine.run(30)
        assert run.wait_world_control(), "the Epoch in hand"


def dial(run, period):
    """In the Epoch: open the time dial (Y) and turn it to `period`."""
    machine = run.machine
    machine.run(4, "Y")
    machine.run(100)
    for _ in range(8):
        if machine.b(DIAL_PERIOD) == period:
            break
        machine.run(4, "UP" if machine.b(DIAL_PERIOD) > period else "DOWN")
        machine.run(56)
    assert machine.b(DIAL_PERIOD) == period, ("the dial at period", period)


def dial_from_the_dock(run, period):
    """In the Epoch docked at the End of Time: Full speed ahead! opens the dial."""
    machine = run.machine
    # No confirm until the choice takes the pad, which would pick a row.
    assert wait_until(machine, lambda: machine.b(CHOICE_ACTIVE) == CHOICE_READY, 3000), "the Epoch's choice"
    machine.run(60)
    assert machine.b(CHOICE_ROW) == FULL_SPEED, "Full speed ahead!"
    machine.run(3, "A")
    machine.run(150)
    for _ in range(8):
        if machine.b(DIAL_PERIOD) == period:
            break
        machine.run(4, "UP" if machine.b(DIAL_PERIOD) > period else "DOWN")
        machine.run(56)
    assert machine.b(DIAL_PERIOD) == period, ("the dial at period", period)


def go_through_time(run, world):
    """Travel (A) with the dial set, through the departure's lines, until the
    party flies the Epoch over `world`."""
    machine = run.machine
    machine.run(4, "A")
    assert run.dialogs_until(lambda: run.map_id() == world, 8000), ("arrived over", world)
    run.wait_world_control()


def entrance(run, location):
    """Where a place's entrance is on this world map, from the map's exit records."""
    records = run.machine.peek(WORLD_EXITS, WORLD_EXIT_BYTES * WORLD_EXIT_COUNT)
    for offset in range(0, len(records), WORLD_EXIT_BYTES):
        record = records[offset:offset + WORLD_EXIT_BYTES]
        if record[0] & 0x80 and int.from_bytes(record[3:5], "little") & 0x1FF == location:
            return (record[0] & 0x7F) * 16, (record[1] & 0x3F) * 16
    raise AssertionError(("no entrance", hex(location)))


def fly_to(run, x, y, near=6):
    """In the air: steer the Epoch over (x, y)."""
    machine = run.machine
    for _ in range(600):
        px, py = run.world_pos()
        if abs(px - x) <= near and abs(py - y) <= near:
            return
        across = "" if abs(px - x) <= near else ("RIGHT" if px < x else "LEFT")
        down = "" if abs(py - y) <= near else ("DOWN" if py < y else "UP")
        machine.run(4, "+".join(b for b in (down, across) if b))
    raise AssertionError(("flying to", (x, y), "stuck at", run.world_pos()))


def land_by(run, location, enter=True):
    """Fly over a place's entrance, come down on open ground beside it, and
    walk in (unless not `enter`)."""
    machine = run.machine
    x, y = entrance(run, location)
    for dx, dy in LANDING_SPOTS:
        fly_to(run, x + dx, y + dy)
        machine.run(20)
        machine.mark(LANDING_SLOT)
        machine.run(3, "A")
        for _ in range(40):
            machine.run(10)
            if machine.b(A.OVERWORLD_TRAVEL) == 0:
                break
        if machine.b(A.OVERWORLD_TRAVEL) == 0:
            machine.free(LANDING_SLOT)
            break
        machine.restore(LANDING_SLOT)
        machine.free(LANDING_SLOT)
    assert machine.b(A.OVERWORLD_TRAVEL) == 0, ("landed by", hex(location))
    run.wait_world_control()
    if enter:
        world_enter(run, location)
