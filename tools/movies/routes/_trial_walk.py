"""Checkpoint searches biased toward a known doorway or waypoint."""
import heapq


def toward(run, x, y, goal, step=16, limit=1800, world=False):
    """Try walks on machine copies; keep only the path reaching the goal.

    A distance bias avoids exploring every corner of Guardia's large rooms.
    Collision and event handling still come entirely from the running game.
    """
    machine = run.machine
    position = run.world_pos if world else run.me
    if goal():
        return
    here = run.map_id()
    root = 4000
    machine.mark(root)
    queue = [(0, 0, root, ())]
    seen = set()
    used = []
    found = None
    try:
        while queue and len(used) < limit:
            _, _, slot, path = heapq.heappop(queue)
            for direction in ("UP", "RIGHT", "DOWN", "LEFT"):
                machine.restore(slot)
                machine.probe(step, direction)
                if world:
                    machine.probe(4)
                px, py = position()
                if goal():
                    found = path + (direction,)
                    break
                if run.map_id() != here:
                    continue
                key = (int(px) // 6, int(py) // 6)
                if key in seen:
                    continue
                seen.add(key)
                child = len(used) + 10
                machine.mark(child)
                used.append(child)
                trail = path + (direction,)
                score = len(trail) * step + 4 * (abs(px - x) + abs(py - y))
                heapq.heappush(queue, (score, child, child, trail))
            if found is not None:
                break
    finally:
        machine.restore(root)
        machine.free(root)
        for slot in used:
            machine.free(slot)
    assert found is not None, ("waypoint unreachable", (x, y), here, len(seen))
    for direction in found:
        machine.run(step, direction)
        if world:
            machine.run(4)
    run.say("toward", (x, y), "steps", len(found), "searched", len(seen),
            "map", run.map_id(), "at", position())


def world_enter(run, location, row=None):
    """Find the destination's entrance in the live overworld exit records."""
    machine = run.machine
    records = machine.peek(0x7E7E00, 7 * 128)
    entrances = []
    for offset in range(0, len(records), 7):
        record = records[offset:offset + 7]
        if (record[0] & 0x80 and int.from_bytes(record[3:5], "little") & 0x1FF == location
                and (row is None or record[1] & 0x3F == row)):
            for dx, dy in ((8, 8), (8, 0), (0, 8), (0, 0)):
                entrances.append(((record[0] & 0x7F) * 16 + dx, (record[1] & 0x3F) * 16 + dy))
    assert entrances, ("no entrance", hex(location))
    wanted = location.to_bytes(2, "little").hex()
    px, py = run.world_pos()
    entrances.sort(key=lambda p: abs(p[0] // 16 * 16 - px) + abs(p[1] // 16 * 16 - py))
    machine.mark(3999)
    try:
        for x, y in entrances:
            machine.restore(3999)
            try:
                toward(run, x, y, lambda: run.world_pos() == (x, y), step=8, limit=400, world=True)
            except AssertionError:
                continue
            machine.run(3, "A")
            machine.run(90)
            if run.map_id() == wanted:
                return
        machine.restore(3999)
        raise AssertionError(("the overworld entrance", hex(location)))
    finally:
        machine.free(3999)


def field_exit(run, x, y, destination):
    """Walk out, allowing farewell scenes on the way to finish first."""
    for _ in range(8):
        assert run.wait_control()
        toward(run, x, y, lambda: run.map_id() == destination or run.dialog()
               or not run.machine.b(0x7E011F))
        if run.map_id() == destination:
            return
        assert run.wait_control(16000)
    raise AssertionError("too many scenes before the doorway")
