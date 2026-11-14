"""The Black Omen's first floors in 1000 A.D., for their treasure, and back out.

From the dock where the Epoch lets the party in, up past the Mega Mutant and the
halls behind it to the teleporter, from the room it leads to into the elevator's
hall, and down in the elevator to the floors with the treasure: a MegaElixir by
the stairs ($139), the Magic Seal ($13A), and the room of six chests ($13B) with
the Haste Helm, the Nova Armor, the Vigil Hat, an Elixir and two MegaElixirs
(cavesofnarshe.com's walkthrough, chapter 33; the chests' places are the ROM's,
bank $F5's lists). Then the same way back to the dock.

The elevator's hall ($063) sends the party on by where the elevator last went
(bits of $7F:01A7 its scripts keep): ridden down from the spot on the hall's
left, its door opens on the treasure's floors; ridden back up from the spot on
its right, on the room with the teleporter, whose beam leads back to the halls
by the Mega Mutant. The monsters on the way, and those that board the elevator,
fall to plain blows with healing first (policies.holding_out); some of them
take next to nothing from techs.
"""
from .. import addresses as A, policies
from .menu_sweep import count_of

DOCK, MEGA_MUTANT_HALL = "c101", "3401"
HALLS = ["3501", "3601", "3701", "3801"]          # the halls between the Mega Mutant and the teleporter
TELEPORTER, ARRIVAL, ELEVATOR_HALL = "6100", "6200", "6300"
RIDES = ("6400", "6500")                          # the elevator's ride down and up
TREASURE_STAIRS, SEAL_HALL, TREASURE_ROOM = "3901", "3a01", "3b01"
DOWN_SPOT, UP_SPOT = (40, 312), (216, 312)        # where the elevator is started, facing its console below
BELOW_THE_TELEPORTER = (136, 160)                 # in the last hall, below the teleporter's door
MEGA_ELIXIR, MAGIC_SEAL, NOVA_ARMOR, ELIXIR, HASTE_HELM, VIGIL_HAT = 0xC5, 0xB6, 0x6B, 0xC3, 0x91, 0x8F
CHESTS = {
    TREASURE_STAIRS: [(27, 19, MEGA_ELIXIR)],
    SEAL_HALL: [(50, 17, MAGIC_SEAL)],
    TREASURE_ROOM: [(2, 7, MEGA_ELIXIR), (2, 16, NOVA_ARMOR), (2, 27, ELIXIR), (13, 7, HASTE_HELM),
                    (13, 16, MEGA_ELIXIR), (13, 27, VIGIL_HAT)],
}
TILE = 16


def here(run):
    return run.machine.peek(A.MAP_ID, 2).hex()


def in_hand(run, frames=3000):
    """Close what windows open until the field takes the pad. Some of the
    Omen's rooms keep the party's sprite where Run.find_player doesn't look,
    so the field's control is what tells."""
    machine = run.machine
    for _ in range(frames // 30):
        run.close_dialogs()
        if machine.b(A.FIELD_PLAYER_CONTROL):
            machine.run(30)
            if machine.b(A.FIELD_PLAYER_CONTROL):
                run.find_player()       # where it can: the walk reads the party from its sprite
                return True
        machine.run(30)
    return False


LEFT_THE_FIGHT = 240           # frames of the field in hand, with no command menu, that end a fight


def fight_on(run, seed, rest=None, on_foot=True):
    """A fight the walk ran into, won with blows and healing first. It is over
    with every enemy down and someone standing, or once the field has stayed in
    hand for a while (the Incognitos leave a fight rather than fall)."""
    machine = run.machine
    calm = [0]

    def over(r, state):
        calm[0] = calm[0] + 1 if machine.b(A.FIELD_PLAYER_CONTROL) and not A.battle_menu_open(machine) else 0
        standing = any(state.hp(slot) for slot in range(3))
        return standing and (not policies.enemies(state) or calm[0] >= LEFT_THE_FIGHT)
    run.fight("holding_out", over, party=3, seed=seed, max_frames=300000)
    # On the elevator the ride goes on after a fight, the field out of hand.
    assert not on_foot or in_hand(run), "the field after the fight"
    if on_foot and rest:
        rest(run)


def advance(run, want, seed, rest=None, limit=12):
    """Walk to map `want`, winning the fights on the way."""
    machine = run.machine
    start = here(run)
    for _ in range(limit):
        if here(run) not in (start, want):
            # A fight's end can leave the party by the door it came in at,
            # and the walk out through it: back in, and on.
            assert run.exit_to(start), ("back into", start)
        moves = run.walk(lambda m, px, py: not m.b(A.FIELD_PLAYER_CONTROL) or m.peek(A.MAP_ID, 2).hex() == want,
                         max_nodes=10000)
        machine.run(60)
        if here(run) == want:
            assert in_hand(run), ("in hand at", want)
            return
        assert moves is not None, ("a way to", want, "from", here(run), run.me())
        if here(run) != start:
            continue
        if run.dialogs_until(lambda: A.battle_menu_open(machine) or bool(machine.b(A.FIELD_PLAYER_CONTROL)), 3000) \
                and A.battle_menu_open(machine):
            fight_on(run, seed, rest)
    raise AssertionError(("reached", want))


def reach(run, spot, seed, rest=None, limit=6):
    """Go to `spot` on this map, winning the fights the way runs into."""
    machine = run.machine
    x, y = spot
    for _ in range(limit):
        if not run.go(x, y, tolerance=4):
            run.walk(lambda m, px, py: (abs(px - x) <= 4 and abs(py - y) <= 4) or not m.b(A.FIELD_PLAYER_CONTROL),
                     max_nodes=10000)
        machine.run(30)
        px, py = run.me()
        if machine.b(A.FIELD_PLAYER_CONTROL) and abs(px - x) <= 4 and abs(py - y) <= 4:
            return
        if run.dialogs_until(lambda: A.battle_menu_open(machine) or bool(machine.b(A.FIELD_PLAYER_CONTROL)), 3000)                 and A.battle_menu_open(machine):
            fight_on(run, seed, rest)
    raise AssertionError(("reached", spot, "at", run.me()))


def into_the_beam(run, beam_room):
    """Up into the light in the middle of `beam_room`, which carries the party
    on; the field in hand where it arrives."""
    machine = run.machine
    for nudge in ("", "LEFT", "RIGHT", "LEFT"):
        if nudge:
            machine.run(2, nudge)       # off a doorway's edge, which stops the party
        for _ in range(400):
            machine.run(1, "UP")
            if here(run) != beam_room or not machine.b(A.FIELD_PLAYER_CONTROL):
                break
        if here(run) != beam_room or not machine.b(A.FIELD_PLAYER_CONTROL):
            break
    for _ in range(60):
        machine.run(30)
        run.close_dialogs()
        if machine.b(A.FIELD_PLAYER_CONTROL) and here(run) != beam_room:
            break
    # The room the beam leads to keeps the party's sprite where the walk
    # already finds it, so the field's control is what tells it has arrived.
    assert here(run) != beam_room and machine.b(A.FIELD_PLAYER_CONTROL), ("carried on from", beam_room)


def ride_the_elevator(run, spot, seed, rest=None):
    """Start the elevator from `spot` in its hall and ride it, winning the
    fights that board it, back to the hall with its door opening elsewhere."""
    machine = run.machine
    reach(run, spot, seed, rest)
    machine.run(2, "DOWN")
    machine.run(3, "A")
    for _ in range(80):
        machine.run(30)
        run.close_dialogs()
        if here(run) in RIDES and machine.b(A.FIELD_PLAYER_CONTROL):
            break
    assert here(run) in RIDES, "aboard the elevator"
    for _ in range(600):
        machine.run(30)
        run.close_dialogs()
        if A.battle_menu_open(machine):
            fight_on(run, seed, rest, on_foot=False)
        if here(run) not in RIDES:
            break
    assert here(run) == ELEVATOR_HALL and in_hand(run), "off the elevator"
    if rest:
        rest(run)


def take_chest(run, tx, ty, item):
    """Open the chest on tile (tx, ty), from whichever side the map allows."""
    machine = run.machine
    before = count_of(machine, item)
    for dx, dy, face in ((0, 1, "UP"), (-1, 0, "RIGHT"), (1, 0, "LEFT"), (0, -1, "DOWN")):
        if not run.go((tx + dx) * TILE + TILE // 2, (ty + dy) * TILE + TILE // 2, tolerance=4):
            continue
        machine.run(2, face)
        machine.run(3, "A")
        machine.run(30)
        run.close_dialogs()
        assert in_hand(run), "the field after the chest"
        if count_of(machine, item) > before:
            return
    raise AssertionError(("the chest at", tx, ty))


def to_the_treasure(run, seed, rest=None):
    """From the dock, the field in hand, to the treasure room with all its
    chests and those on the way opened; `rest(run)`, where given, readies the
    party after each fight on foot."""
    for want in [MEGA_MUTANT_HALL] + HALLS:
        advance(run, want, seed, rest)
    # The teleporter's door is straight up the last hall's middle.
    reach(run, BELOW_THE_TELEPORTER, seed, rest)
    into_the_beam(run, HALLS[-1])
    into_the_beam(run, TELEPORTER)
    advance(run, ELEVATOR_HALL, seed, rest)
    ride_the_elevator(run, DOWN_SPOT, seed, rest)
    for want in (TREASURE_STAIRS, SEAL_HALL, TREASURE_ROOM):
        advance(run, want, seed, rest)
        for tx, ty, item in CHESTS[want]:
            take_chest(run, tx, ty, item)


def back_to_the_dock(run, seed, rest=None):
    """From the treasure room the way the party came, to the dock."""
    for want in (SEAL_HALL, TREASURE_STAIRS, ELEVATOR_HALL):
        advance(run, want, seed, rest)
    ride_the_elevator(run, UP_SPOT, seed, rest)
    advance(run, ARRIVAL, seed, rest)
    into_the_beam(run, ARRIVAL)
    for want in list(reversed(HALLS)) + [MEGA_MUTANT_HALL, DOCK]:
        advance(run, want, seed, rest)
