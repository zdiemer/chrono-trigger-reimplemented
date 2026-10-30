"""Shared approach and dialogue helpers for the Millennial Fair games."""
from .. import bfs

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
SQUARE, TENT = "0500", "b201"
POINTS = 0x7F0052
CONTROL = 0x7E011F
CHOICE = 0x7E0162


def variable(machine, index):
    return machine.b(0x7F0200 + 2 * index)


def nearby(run, x, y, tolerance=12):
    """Short walks keep the search's machine copies bounded on the large square."""
    moves, _ = bfs.bfs_walk(
        run.machine, lambda m, px, py: abs(px - x) < tolerance and abs(py - y) < tolerance,
        run._pos(), run._map(), step=16, cell=12, max_nodes=500)
    assert moves is not None, "walk to %d, %d" % (x, y)
    run.say("walk", (x, y), "at", run.me(), "frame", run.machine.frame)


def square(run):
    m = run.machine
    m.run(1620)
    m.run(5, "START")
    m.run(2420 - m.frame)
    assert run.select_slot(0), "ctsave14 slot 1"
    m.run(4, "A")
    for _ in range(200):
        m.run(10)
        if run.map_id() not in ("0000", "b101"):
            break
    m.run(120)
    assert run.map_id() == "f001", "the 1000 A.D. world map"
    assert run.world_go(424, 224), "Leene Square's world-map entrance"
    m.run(4, "A")
    m.run(120)
    assert run.map_id() == SQUARE and run.wait_control(), "the lower square in hand"


def tent(run):
    for point in ((392, 640), (392, 560), (456, 528), (536, 528), (616, 528), (680, 480)):
        nearby(run, *point)
    moves, _ = bfs.bfs_walk(run.machine, lambda m, x, y: run.map_id() == TENT,
                            run._pos(), run._map(), step=16, cell=12, max_nodes=180, push=45)
    assert moves is not None and run.wait_control(), "Bekkler's tent in hand"


def choose(run, line):
    """Advance the greeting and wait until the choices actually accept the pad."""
    m = run.machine
    for _ in range(100):
        if m.b(CHOICE) == 2:
            break
        if run.dialog():
            m.run(3, "A")
            m.run(27)
        else:
            m.run(10)
    assert m.b(CHOICE) == 2, "Bekkler's choice ready"
    for _ in range(4):
        if m.b(0x7E0163) == line:
            break
        m.run(3, "DOWN")
        m.run(20)
    assert m.b(0x7E0163) == line, "the requested game selected"
    m.run(3, "A")
    m.run(50)


def greet(run):
    # Object $0B function 1 accepts a talk while local $0B is zero. Its
    # move to tile (8,5) precedes the greeting; no window opens immediately.
    assert variable(run.machine, 0x0B) == 0, "no game already running"
    assert run.go(134, 149, tolerance=3), "below Bekkler, within talking reach"
    m = run.machine
    m.run(2, "UP")
    m.run(8)
    m.run(3, "A")
    for _ in range(200):
        if run.dialog():
            return
        m.run(10)
    raise AssertionError("Bekkler's greeting")


def header(game, description):
    return ("# " + description + " (route fair_" + game + ").\n"
            "# Save: ctsave14 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).\n"
            "# frames {FRAMES}\n# sram crc32 {CRC}\n# <start_frame> <frame_count> <buttons>").splitlines()
