"""Breadth-first walking searches over machine checkpoints.

Instead of timing a walk, try the four directions from a checkpoint, keep the
positions they reach, and search until the goal holds. The machine is the map:
walls, NPCs and one-way exits need no model. Each node is one in-memory slot
(`Machine.mark`), so the search costs slots, not frames, and only the path that
wins is replayed with `Machine.run` - that is what ends up in the movie.

Two flavors:

    bfs_walk  field maps: `step` frames per move, positions bucketed into `cell`
              pixel cells. `push` > 0 also tries holding a blocked direction for
              that many frames, for exits that only trigger after a while.
    ow_bfs    the overworld, which moves in whole 16-pixel tiles: hold a
              direction, release, and use the tile position as the node.

Both take `pos(machine) -> (x, y)` and `map_id(machine) -> hashable`; reaching
another map is a node too, so "walk to the next map" is just a goal.
"""
from collections import deque

DIRECTIONS = ("UP", "DOWN", "LEFT", "RIGHT")


class _Slots:
    """Hands out in-memory checkpoint slots and frees them at the end."""

    def __init__(self, machine, root, first, limit):
        self.machine, self.root, self.next, self.limit = machine, root, first, limit
        self.used = []

    def take(self):
        if self.next >= self.limit:
            return None
        slot = self.next
        self.next += 1
        self.used.append(slot)
        self.machine.mark(slot)
        return slot

    def release(self):
        for slot in self.used:
            self.machine.free(slot)
        self.used = []


def _search(machine, goal, pos, map_id, move, node_of, max_nodes, root_slot, slot_base, slot_limit, extra):
    """Shared core: returns (moves, visited) with the machine at the goal or back at the root."""
    root_frame = machine.frame
    machine.mark(root_slot)
    slots = _Slots(machine, root_slot, slot_base, slot_limit)
    start = node_of(pos(machine), map_id(machine))
    parent = {start: None}
    slot_of = {start: root_slot}
    queue = deque([start])
    found = None
    if goal(machine, *pos(machine)):
        found = start
    while queue and found is None and len(parent) < max_nodes:
        node = queue.popleft()
        for direction in DIRECTIONS:
            machine.restore(slot_of[node])
            move(machine, direction)
            key = node_of(pos(machine), map_id(machine))
            if key == node and extra:
                # blocked: some exits need the direction held for longer
                extra(machine, direction)
                moved = node_of(pos(machine), map_id(machine))
                if moved != node:
                    key, direction = ("held", moved, node, direction), direction + "*"
                    parent[key] = (node, direction)
                    if goal(machine, *pos(machine)):
                        found = key
                        break
                continue
            if key in parent:
                continue
            parent[key] = (node, direction)
            if goal(machine, *pos(machine)):
                found = key
                break
            if key[0] == "map":
                continue          # another map: reachable, but don't walk on in it
            slot = slots.take()
            if slot is None:
                break
            slot_of[key] = slot
            queue.append(key)
    slots.release()
    machine.restore(root_slot)
    machine.free(root_slot)
    machine.frame = root_frame
    visited = [key for key in parent if key[0] not in ("map", "held")]
    if found is None:
        return None, visited
    moves = []
    key = found
    while parent[key] is not None:
        key, direction = parent[key]
        moves.append(direction)
    moves.reverse()
    return moves, visited


def bfs_walk(machine, goal, pos, map_id, step=8, cell=6, max_nodes=3000, push=0,
             root_slot=1, slot_base=2, slot_limit=4096):
    """Walk until `goal(machine, x, y)`. Returns the moves taken, or None.

    On success the machine is left at the goal with the walk logged as input.
    """
    def node_of(position, current_map):
        if current_map != start_map:
            return ("map", current_map)
        return (int(position[0]) // cell, int(position[1]) // cell)

    def move(m, direction):
        m.probe(step, direction)

    def hold_longer(m, direction):
        m.probe(push, direction)

    start_map = map_id(machine)
    moves, visited = _search(machine, goal, pos, map_id, move, node_of, max_nodes, root_slot, slot_base,
                             slot_limit, hold_longer if push else None)
    if moves is None:
        return None, visited
    for direction in moves:
        machine.run(step, direction.rstrip("*"))
        if direction.endswith("*"):
            machine.run(push, direction.rstrip("*"))
    return moves, visited


def ow_bfs(machine, goal, pos, map_id, hold=16, rest=4, max_nodes=2500, confirm=False,
           root_slot=1, slot_base=2, slot_limit=4096):
    """The same on the overworld, where one move is one 16-pixel tile.

    `confirm`: also press A after each step and wait, since the overworld only
    enters the place an exit leads to when the leader stands on it and confirms
    (game/overworld/walk.c); without it a tile the goal only reaches by walking
    onto an exit is never recognized as reached. The search presses A at every
    tile it tries (any tile might be the exit); the replay presses it only on
    the last step, since that is the only tile confirming does anything -- a
    walk that pressed it the whole way wouldn't be ordinary play."""
    def node_of(position, current_map):
        if current_map != start_map:
            return ("map", current_map)
        return (int(position[0]), int(position[1]))

    def move(m, direction):
        m.probe(hold, direction)
        m.probe(rest)
        if confirm:
            m.probe(3, "A")
            m.probe(30)

    start_map = map_id(machine)
    moves, visited = _search(machine, goal, pos, map_id, move, node_of, max_nodes, root_slot, slot_base,
                             slot_limit, None)
    if moves is None:
        return None, visited
    for index, direction in enumerate(moves):
        machine.run(hold, direction)
        machine.run(rest)
        if confirm and index == len(moves) - 1:
            machine.run(3, "A")
            machine.run(30)
    return moves, visited
