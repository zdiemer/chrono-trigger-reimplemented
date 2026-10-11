"""What a character should do, given the battle state.

A policy is `f(bot, state, rng, inventory) -> action` for `Bot.do`. They are
deliberately loose: every choice is a coin flip around a threshold, so the route
runner's backtracking (campaign.Run.fight) can retry a losing fight with other
choices instead of needing a policy that is right the first time.

Item indices are computed from the inventory, tech indices from the in-battle
tech list, so a policy never assumes a fixed menu layout.
"""
from . import addresses as A

# Consumables, in inventory order ($BD-$C7).
TONIC, MID_TONIC, FULL_TONIC, ETHER, MID_ETHER, FULL_ETHER, ELIXIR, HYPER_ETHER, MEGA_ELIXIR, HEAL, REVIVE = \
    range(0xBD, 0xC8)

# Items the battle item list shows; it skips the rest of the inventory.
BATTLE_USABLE = set(range(0xBD, 0xC8)) | {0xCA, 0xCB, 0xCC}

# Tech list entries per character id, as index and MP cost:
#   revive, heal_all, heal_one, all (hit every enemy), one (hit one enemy)
# The lists are tried in order and skipped when the MP isn't there or the entry
# isn't learned yet (tech_known); attacking is always the fallback.
CHARACTER_TECHS = {
    0: dict(revive=(5, 10), all=[(7, 20), (4, 15)], one=[(2, 2), (1, 2)]),                      # Crono
    1: dict(revive=(7, 15), heal_one=(6, 5), one=[(5, 8), (2, 2)]),                             # Marle
    2: dict(all=[(7, 20), (6, 15), (3, 3)], one=[(5, 8), (2, 2)]),                              # Lucca
    3: dict(heal_all=(4, 3), heal_one=(1, 2), all=[(6, 14), (2, 3)], one=[(5, 12), (0, 1)]),    # Robo
    4: dict(heal_all=(5, 10), heal_one=(3, 2)),                                                 # Frog
    5: dict(),                                                                                  # Ayla
    6: dict(all=[(7, 20), (5, 10)], one=[(0, 8), (1, 8), (2, 8)]),                              # Magus
}


def item_index(inventory, item):
    """Where `item` sits in the battle item list, or None when there is none."""
    items, counts = inventory
    index = 0
    for slot in range(len(items)):
        if items[slot] in BATTLE_USABLE and counts[slot] > 0:
            if items[slot] == item:
                return index
            index += 1
    return None


def use(inventory, item, target):
    index = item_index(inventory, item)
    return None if index is None else ("item", index, target)


def tech_known(bot, member, index):
    """False when list entry `index` is a blank row (see addresses.BATTLE_TECH_LIST)."""
    base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
    return bot.machine.b(A.BATTLE_TECH_LIST + base + index) != 0xFF


def enemies(state):
    """Enemy slots that are alive; the 1 HP dummies and the party are skipped."""
    return [slot for slot in range(3, 11) if 1 < state.max_hp(slot) < 65000 and state.hp(slot) > 0]


def fraction(state, slot):
    return state.hp(slot) / max(1, state.max_hp(slot))


def party_ids(bot):
    return bot.machine.peek(A.PARTY_MEMBERS, 3)


def survival(state, rng, inventory, member, members, healer_threshold=0.6, self_threshold=0.4,
             cure=None, life=None):
    """Revive and heal, the part every policy shares. None when nobody needs it.

    `cure` and `life` are this character's (tech index, MP cost) for healing one
    ally and for reviving, when they have them.
    """
    alive = [slot for slot in members if state.hp(slot) > 0]
    for dead in [slot for slot in members if state.hp(slot) == 0]:
        if life and state.mp(member) >= life[1] and rng.random() < 0.9:
            return ("tech", life[0], dead)
        action = use(inventory, REVIVE, dead)
        if action and rng.random() < 0.95:
            return action
    if not alive:
        return None
    lowest = min(alive, key=lambda slot: fraction(state, slot))
    threshold = healer_threshold + 0.15 * (rng.random() - 0.5)
    if len(alive) >= 2 and sum(fraction(state, slot) < threshold for slot in alive) >= 2 and rng.random() < 0.85:
        action = use(inventory, MEGA_ELIXIR, None)
        if action:
            return action
    if fraction(state, lowest) < (threshold if cure else self_threshold):
        roll = rng.random()
        if cure and state.mp(member) >= cure[1] and roll < 0.35:
            return ("tech", cure[0], lowest)
        action = use(inventory, FULL_TONIC if roll < 0.7 else ELIXIR, lowest)
        if action:
            return action
    return None


def party(bot, state, rng, inventory):
    """Table-driven policy for any party: revive, heal, strongest known tech, attack."""
    member = state.member
    members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
    ids = party_ids(bot)
    techs = CHARACTER_TECHS.get(ids[member] if ids[member] < 7 else 0, {})
    alive = [slot for slot in members if state.hp(slot) > 0]
    dead = [slot for slot in members if state.hp(slot) == 0]

    for slot in dead:
        revive = techs.get("revive")
        if revive and state.mp(member) >= revive[1] and tech_known(bot, member, revive[0]) and rng.random() < 0.7:
            return ("tech", revive[0], slot)
        action = use(inventory, REVIVE, slot)
        if action and rng.random() < 0.9:
            return action
    threshold = 0.5 + 0.2 * rng.random()
    low = [slot for slot in alive if fraction(state, slot) < threshold]
    if len(low) >= 2:
        heal_all = techs.get("heal_all")
        if heal_all and state.mp(member) >= heal_all[1] and tech_known(bot, member, heal_all[0]) \
                and rng.random() < 0.6:
            return ("tech", heal_all[0], None)
        action = use(inventory, MEGA_ELIXIR, None)
        if action and rng.random() < 0.8:
            return action
    if low:
        worst = min(low, key=lambda slot: fraction(state, slot))
        if fraction(state, worst) < 0.4 or rng.random() < 0.5:
            heal_one = techs.get("heal_one")
            if heal_one and state.mp(member) >= heal_one[1] and tech_known(bot, member, heal_one[0]) \
                    and rng.random() < 0.5:
                return ("tech", heal_one[0], worst)
            action = use(inventory, FULL_TONIC if rng.random() < 0.7 else ELIXIR, worst)
            if action:
                return action
    if state.mp(member) < 10 and rng.random() < 0.3:
        action = use(inventory, HYPER_ETHER, member)
        if action:
            return action

    living = enemies(state)
    if not living:
        return ("attack", None)
    target = min(living, key=lambda slot: state.hp(slot)) if rng.random() < 0.6 else rng.choice(living)
    roll = rng.random()
    if len(living) > 1 or roll < 0.5:
        for index, cost in techs.get("all", []):
            if state.mp(member) >= cost and tech_known(bot, member, index) and rng.random() < 0.85:
                return ("tech", index, None)
    for index, cost in techs.get("one", []):
        if state.mp(member) >= cost and tech_known(bot, member, index) and rng.random() < 0.6:
            return ("tech", index, target)
    return ("attack", target)


def crono_and_marle(bot, state, rng, inventory):
    """Crono and Marle (the New Game+ telepod route): Luminaire and Ice 2, Marle heals."""
    member = state.member
    living = enemies(state) or [10]
    if member == 0:
        action = survival(state, rng, inventory, 0, (0, 1), life=(5, 10))
        if action:
            return action
        if state.mp(0) >= 20:
            return ("tech", 7, None)                       # Luminaire
        if rng.random() < 0.8:
            action = use(inventory, HYPER_ETHER, 0)
            if action:
                return action
        return ("attack", rng.choice(living))
    action = survival(state, rng, inventory, 1, (0, 1), cure=(6, 5), life=(7, 15))
    if action:
        return action
    if state.hp(0) > 0 and state.mp(0) < 20 and rng.random() < 0.8:
        action = use(inventory, HYPER_ETHER, 0)
        if action:
            return action
    roll = rng.random()
    if roll < 0.2 and state.hp(0) > 0:
        return ("tech", 4, 0)                              # Haste on Crono
    if roll < 0.6 and state.mp(1) >= 8:
        return ("tech", 5, rng.choice(living))             # Ice 2
    return ("attack", rng.choice(living))


def crono_and_marle_core(bot, state, rng, inventory):
    """The Lavos Core with two characters: break the body and the left bit first.

    The core (slot 9) takes about one percent of a hit while the body (slot 3)
    and the left bit (slot 7) are alive, so everything goes into those first;
    they revive, so the window on the core is short and Marle keeps Crono topped
    up (the body's big spells hit for most of his HP).
    """
    member = state.member
    standing = [slot for slot in (3, 7) if state.hp(slot) > 0]
    core_open = not standing
    if member == 0:
        action = survival(state, rng, inventory, 0, (0, 1), life=(5, 10))
        if action:
            return action
        if state.mp(0) >= 20 and (not core_open or rng.random() < 0.5):
            return ("tech", 7, None)
        if state.mp(0) < 20 and not core_open and rng.random() < 0.5:
            action = use(inventory, HYPER_ETHER, 0)
            if action:
                return action
        if core_open:
            return ("attack", 9)
        return ("attack", 7 if 7 in standing and rng.random() < 0.6 else rng.choice(standing))

    action = survival(state, rng, inventory, 1, (0, 1), healer_threshold=0.6, cure=(6, 5), life=(7, 15))
    if action:
        return action
    if state.hp(0) > 0 and state.hp(0) < state.max_hp(0) - 150 and rng.random() < 0.8:
        action = use(inventory, FULL_TONIC, 0)
        if action:
            return action
    if state.hp(1) < state.max_hp(1) - 250 and rng.random() < 0.6:
        action = use(inventory, FULL_TONIC, 1)
        if action:
            return action
    if state.hp(0) > 0 and state.mp(0) < 20 and not core_open and rng.random() < 0.7:
        action = use(inventory, HYPER_ETHER, 0)
        if action:
            return action
    roll = rng.random()
    if roll < 0.1 and state.hp(0) > 0:
        return ("tech", 4, 0)
    if core_open:
        return ("attack", 9) if roll < 0.7 or state.mp(1) < 8 else ("tech", 5, 9)
    target = 7 if 7 in standing and rng.random() < 0.6 else rng.choice(standing)
    if roll < 0.5 and state.mp(1) >= 8:
        return ("tech", 5, target)
    return ("attack", target)


def tech_list_entries(bot, member):
    """`(entry, tech_id)` for every real tech in `member`'s tech list, in list
    order (singles, then the double techs' section, then the triple techs'):
    the section headings and blank rows are skipped. Reads straight from the
    battle's tech-by-id table (addresses.BATTLE_TECH_LIST_IDS), so it needs no
    table of which combination techs a party can make -- whatever the game's
    own setup put in the list is what this walks."""
    base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
    ids = bot.machine.peek(A.BATTLE_TECH_LIST_IDS + base, A.TECHS_A_MEMBER)
    return [(entry, tech_id) for entry, tech_id in enumerate(ids) if tech_id < 0xFB]


def tech_sweep(cast_ids, healer_threshold=0.5, self_threshold=0.35):
    """A policy that casts every tech a party's members' lists can show, once
    each: on a member's turn, the next tech list entry whose id isn't in
    `cast_ids` yet (singles before doubles before triples, as the list orders
    them), or an attack once its whole list has been tried. Heals and revives
    with items first, to keep a long fight going. `cast_ids` is the caller's
    set, read fresh every turn, so several members (and a double or triple
    tech's partners) share the one record of what has already been cast; wrap
    `Bot.do` (see tools/movies/routes/tech_sweep.py) to add to it when a tech
    is actually taken, since a dimmed entry here is skipped, not cast.
    """
    def policy(bot, state, rng, inventory):
        member = state.member
        members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
        action = survival(state, rng, inventory, member, members,
                          healer_threshold=healer_threshold, self_threshold=self_threshold)
        if action:
            return action
        for entry, tech_id in tech_list_entries(bot, member):
            if tech_id not in cast_ids:
                return ("tech", entry, None)
        living = enemies(state)
        return ("attack", rng.choice(living) if living else None)
    return policy


def tracking_do(bot, cast_ids):
    """Wrap `bot.do` so a tech the bot actually takes (not one tried and found
    dimmed) is added to `cast_ids` by id, not by list position: the member and
    the list entry are read before the action runs (`bot.do` moves the menu on
    to the next member once it succeeds), then turned into a tech id through
    the same table `tech_list_entries` reads."""
    original = bot.do

    def do(action):
        member = bot.state.member
        ok = original(action)
        if ok and action[0] == "tech":
            base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
            tech_id = bot.machine.b(A.BATTLE_TECH_LIST_IDS + base + action[1])
            if tech_id < 0xFB:
                cast_ids.add(tech_id)
        return ok
    bot.do = do


POLICIES = {
    "party": party,
    "crono_and_marle": crono_and_marle,
    "crono_and_marle_core": crono_and_marle_core,
}
