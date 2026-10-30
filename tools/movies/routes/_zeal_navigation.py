"""Checkpoint searches for Zeal's long walks and Mt. Woe's overlapping paths.

The map level belongs to a field node: the same position on a bridge and
under it can lead to different places. A distance priority keeps searches
aimed at a waypoint; only the successful path is replayed into the movie.
"""
import heapq

def approach(run, x, y, *, world=False, limit=3900):
    machine = run.machine
    if world:
        pos = lambda: run.world_pos()
        # World-map steps are eight pixels. Longer presses can skip a narrow
        # junction or the only tile from which an entrance accepts confirm.
        step, rest, cell = 8, 8, 8
        priority = 3.0
        directions = ("UP", "RIGHT", "DOWN", "LEFT")
    else:
        assert run.find_player() is not None, "a player for the walking search"
        pos = run.me
        step, rest, cell = 8, 0, 6
        priority = 1.5
        directions = ("UP", "RIGHT", "DOWN", "LEFT", "UP+RIGHT", "UP+LEFT", "DOWN+RIGHT", "DOWN+LEFT")

    def key(px, py):
        return (int(px) // cell, int(py) // cell, 0 if world else machine.b(0x7E0155))

    def distance(px, py):
        return abs(px - x) + abs(py - y)

    here = run.map_id()
    machine.mark(4000)
    machine.mark(1)
    slots, parents, moves = [1], {1: None}, []
    px, py = pos()
    queue = [(distance(px, py), 0, 1)]
    seen = {key(px, py)}
    found = None
    already = abs(px - x) < 6 and abs(py - y) < 6
    try:
        while queue and len(slots) < limit and not already and found is None:
            _, cost, slot = heapq.heappop(queue)
            for direction in directions:
                machine.restore(slot)
                machine.probe(step, direction)
                if rest:
                    machine.probe(rest)
                px, py = pos()
                if run.map_id() != here:
                    continue
                if abs(px - x) < 6 and abs(py - y) < 6:
                    found = (slot, direction)
                    break
                node = key(px, py)
                if node in seen:
                    continue
                seen.add(node)
                next_slot = len(slots) + 1
                if next_slot >= limit:
                    break
                machine.mark(next_slot)
                slots.append(next_slot)
                parents[next_slot] = (slot, direction)
                elapsed = cost + step + rest
                heapq.heappush(queue, (elapsed + priority * distance(px, py), elapsed, next_slot))
        if found:
            slot, direction = found
            moves.append(direction)
            while parents[slot]:
                slot, direction = parents[slot]
                moves.append(direction)
    finally:
        machine.restore(4000)
        for slot in slots:
            machine.free(slot)
        machine.free(4000)
    run.say("approach", (x, y), "nodes", len(slots), "steps", len(moves))
    assert found or already, "walk to %s" % ((x, y),)
    for direction in reversed(moves):
        machine.run(step, direction)
        machine.run(rest)
