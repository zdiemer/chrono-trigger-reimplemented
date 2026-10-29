"""Walking into a dungeon's fight: the field's enemies stand about as objects and
attack when the party comes near, so a route that wants a fight walks up to the
nearest objects in turn until a battle's first menu opens, and moves on to the
next map when none of them does.

Used by the routes that play a fight of a place no other movie fights in
(routes/denadoro_fight.py).
"""
from . import policies
from .addresses import OBJECT_X, OBJECT_Y

BATTLE_MENU_MEMBER = 0x7E95D5   # whose battle menu is open, $80 and up for none
OFF_SCREEN = 4000               # objects off the map park at $FF00 (4080 px)
WORLD_MAPS = range(0x1F0, 0x200)


def load_slot(run, slot):
    """From power-on: the title, the load screen, save slot `slot` loaded."""
    machine = run.machine
    machine.run(1620)
    machine.run(5, "START")
    machine.run(2420 - machine.frame)
    run.select_slot(slot)
    machine.run(4, "A")
    for _ in range(200):
        machine.run(10)
        if run.map_id() not in ("0000", "b101"):
            break
    machine.run(120)
    run.say("loaded map", run.map_id(), "frame", machine.frame)


def on_the_world_map(run):
    return int.from_bytes(bytes.fromhex(run.map_id()), "little") in WORLD_MAPS


def enter_from_the_world_map(run):
    """Into the location the party stands at on a world map (if it is on one), then the
    party in hand."""
    if on_the_world_map(run) and not run.world_exit():
        return False
    return run.wait_control(3000)


def in_battle(machine):
    return machine.b(BATTLE_MENU_MEMBER) < 0x80


def wait_for_battle(run, frames):
    """Advance up to `frames`, answering any window; True once a battle's menu is open."""
    machine = run.machine
    for _ in range(frames // 20):
        if in_battle(machine):
            return True
        if run.dialog():
            machine.run(3, "A")
        machine.run(17)
    return in_battle(machine)


def walk_into_a_fight(run, rounds=3, per_round=8):
    """Walk up to the objects nearest the party, nearest first, until one starts a
    battle; after `per_round` of them with none, walk on into another map and try
    there. True with the battle's first menu open."""
    machine = run.machine
    tried = set()
    for _ in range(rounds):
        me = run.me()
        near = []
        for index in range(2, 0x40):
            x, y = run.object_pos(index)
            if x < OFF_SCREEN and index != run.player and index not in tried:
                near.append(((x - me[0]) ** 2 + (y - me[1]) ** 2, index))
        for _, index in sorted(near)[:per_round]:
            tried.add(index)

            def beside(m, px, py, index=index):
                if m.b(BATTLE_MENU_MEMBER) < 0x80 or run.dialog():
                    return True
                ox, oy = m.w(OBJECT_X + 2 * index) / 16, m.w(OBJECT_Y + 2 * index) / 16
                return abs(px - ox) <= 14 and abs(py - oy) <= 14
            moves = run.walk(beside, max_nodes=1500)
            run.say("toward object", index, "steps", moves and len(moves), "frame", machine.frame)
            if moves is None:
                continue
            if wait_for_battle(run, 400):
                return True
            run.wait_control(1500)
        if not run.exit_other(frozenset()):
            return False
        run.wait_control(3000)
    return False


def walk_into_a_fight_from(run, checkpoint):
    """walk_into_a_fight, and if that finds none, back to `checkpoint` (the run's state
    file of that name) for a wider search: more objects a map, more maps. The first
    search is the one the movies were made with; the wider one is for the shipping
    mode, whose timing has the monsters elsewhere when the party comes by."""
    if walk_into_a_fight(run):
        return True
    run.say("no fight; a wider search from", checkpoint)
    return run.resume(checkpoint) and walk_into_a_fight(run, rounds=5, per_round=16)


def win_the_fight(run, seed):
    """Fight with the battle bot until no enemy is left standing and some of the party is."""
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(slot) for slot in range(3))
    run.fight("party", won, party=3, seed=seed, max_frames=60000)
