"""Shared loading, healing and victory checks for the early story bosses."""
from .. import policies

BATTLE_MENU_MEMBER = 0x7E95D5
STORY = 0x7F0000
PLAYER_CONTROL = 0x7E011F


def settle(run, seed=1):
    """A script can briefly enable movement while still staging a battle."""
    machine = run.machine
    start, stable = machine.frame, 0
    while machine.frame - start < 10000:
        if machine.b(BATTLE_MENU_MEMBER) < 0x80:
            fight(run, seed=seed)
            return
        if machine.b(PLAYER_CONTROL) and not run.dialog() and run.find_player() is not None:
            stable += 1
            if stable == 4:
                return
        else:
            stable = 0
        if run.dialog():
            machine.run(3, "A")
            machine.run(37)
        else:
            machine.run(30)
    raise AssertionError("the encounter or field did not settle")


def advance(run, direction, goal, max_frames=2000, interact=False):
    """Walk until a position, door or scripted encounter takes control."""
    machine = run.machine
    assert run.wait_control(), "the field before walking"
    start = machine.frame
    still = 0
    while not goal() and machine.b(PLAYER_CONTROL) and machine.frame - start < max_frames:
        before = run.me()
        machine.run(4, direction)
        still = still + 4 if run.me() == before else 0
        if interact and still >= 48:
            machine.run(3, "A")
            machine.run(30)
            still = 0
    return goal() or not machine.b(PLAYER_CONTROL)


def walk_through(run, direction, goal, seed=1, interact=False):
    """Follow a corridor, winning encounters and allowing door scenes to finish."""
    for _ in range(8):
        assert advance(run, direction, goal, interact=interact), ("blocked walking", direction, run.map_id(), run.me())
        if goal():
            return
        settle(run, seed)
    raise AssertionError("too many encounters in one corridor")


def search_through(run, goal, seed=1, inside=lambda x, y: True):
    """Search a room, stopping to resolve scripted encounters on the way."""
    from .. import bfs
    machine = run.machine
    for _ in range(8):
        assert run.wait_control(), "the room in hand"
        moves, visited = bfs.bfs_walk(machine,
            lambda m, x, y: inside(x, y) and (goal(m, x, y) or not m.b(PLAYER_CONTROL)),
            run._pos(), lambda m: (run.map_id(), m.b(PLAYER_CONTROL), inside(*run.me())),
            step=16, cell=6, max_nodes=3000)
        run.say("room search", "steps", None if moves is None else len(moves),
                "visited", len(visited), "at", run.me(), "frame", machine.frame)
        assert moves is not None, "a way through the room"
        if goal(machine, *run.me()):
            return
        settle(run, seed)
    raise AssertionError("too many encounters in one room")


def load(run, slot, location, story):
    """Load a cartridge slot and check that it precedes the intended event."""
    machine = run.machine
    machine.run(1620)
    machine.run(5, "START")
    machine.run(2420 - machine.frame)
    assert run.select_slot(slot), "the requested save slot"
    machine.run(4, "A")
    for _ in range(200):
        machine.run(10)
        if run.map_id() not in ("0000", "b101"):
            break
    machine.run(180)
    assert run.map_id() == location, "the save's location"
    assert machine.b(STORY) == story, "the save's story progress"
    run.say("loaded", run.map_id(), "story", hex(story), "frame", machine.frame)


def fast_settings(run):
    """Choose the fastest battle and message speeds through the settings UI."""
    from .menu_sweep import open_menu, open_option, press, back_to_main_menu, SETTINGS
    machine = run.machine
    open_menu(machine)
    open_option(machine, SETTINGS)
    # Settings have two columns of five options; speeds heads the right one.
    for _ in range(12):
        cursor = machine.b(0x7E0054)
        if cursor == 5:
            break
        press(machine, "RIGHT" if cursor < 5 else "UP")
    assert machine.b(0x7E0054) == 5, "the speeds option"
    press(machine, "A", 40)
    assert machine.b(0x7E0054) == 10, "the battle speed meter"
    for _ in range(8):
        if machine.b(0x7E0D8B) == 0:
            break
        press(machine, "LEFT")
    press(machine, "DOWN")
    assert machine.b(0x7E0054) == 11, "the message speed meter"
    for _ in range(8):
        if machine.b(0x7E0D90) == 0:
            break
        press(machine, "LEFT")
    assert machine.b(0x7E0D8B) == machine.b(0x7E0D90) == 0
    back_to_main_menu(machine)
    press(machine, "B", 60)
    assert machine.b(0x7E2990) & 7 == 0 and (machine.b(0x7E2991) >> 3) & 7 == 0


def early_party(bot, state, rng, inventory):
    """Use the Tonics actually available before the late-game healing items."""
    members = [slot for slot in range(bot.party) if state.max_hp(slot)]
    if members and all(state.hp(slot) for slot in members):
        lowest = min(members, key=lambda slot: policies.fraction(state, slot))
        if policies.fraction(state, lowest) < 0.5 + rng.random() * 0.15:
            for item in (policies.MID_TONIC, policies.TONIC):
                action = policies.use(inventory, item, lowest)
                if action:
                    return action
    if any(state.hp(slot) == 0 for slot in members):
        return policies.party(bot, state, rng, inventory)
    character = policies.party_ids(bot)[state.member]
    living = policies.enemies(state)
    if not living:
        return ("attack", None)
    target = min(living, key=lambda slot: state.hp(slot))
    if character in (0, 2) and state.mp(state.member) < 2:
        action = policies.use(inventory, policies.ETHER, state.member)
        if action:
            return action
    if character == 0 and state.mp(state.member) >= 2:
        return ("tech", 0 if len(living) > 1 else 1, target)
    if character == 2 and state.mp(state.member) >= 1:
        return ("tech", 0, target)
    return ("attack", target)


def fight(run, seed=1, party=3, policy=early_party, after=True):
    """Win one battle, then wait for the field to answer the pad again."""
    assert run.dialogs_until(lambda: run.machine.b(BATTLE_MENU_MEMBER) < 0x80
                            and run.machine.w(0x7E5E32) > 0, 8000), "a battle menu"
    seen = False

    def won(r, state):
        nonlocal seen
        living = policies.enemies(state)
        seen |= bool(living)
        return seen and not living and any(state.hp(slot) for slot in range(party))

    run.fight(policy, won, party=party, seed=seed, max_frames=35000, max_tries=300)
    if after:
        assert run.wait_control(12000), "the field back in hand"
