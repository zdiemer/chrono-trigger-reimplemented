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


LAPIS = 0xCA


def party_with_lapis(bot, state, rng, inventory):
    """`party` for a bag from before the late healing items, which `party` reaches for (Full
    Tonics, Elixirs, Mega Elixirs): a Lapis when two or more are low, a Mid Tonic when one is.
    The Black Tyrano's countdown flame leaves Crono, Ayla and Marle at a tenth of their HP on
    ctsave08, and unhealed the next one wipes them."""
    members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
    if any(state.hp(slot) == 0 for slot in members):
        return party(bot, state, rng, inventory)            # revive first
    threshold = 0.45 + 0.2 * rng.random()
    low = [slot for slot in members if fraction(state, slot) < threshold]
    if len(low) >= 2 and rng.random() < 0.9:
        action = use(inventory, LAPIS, None)
        if action:
            return action
    if low and rng.random() < 0.7:
        action = use(inventory, MID_TONIC, min(low, key=lambda slot: fraction(state, slot)))
        if action:
            return action
    return party(bot, state, rng, inventory)


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


# The rows of one member's tech list (addresses.BATTLE_TECH_LIST_IDS).
TECHS_A_MEMBER = 20


def tech_list_entries(bot, member):
    """`(entry, tech_id)` for every real tech in `member`'s tech list, in list
    order (singles, then the double techs' section, then the triple techs'):
    the section headings and blank rows are skipped. Reads straight from the
    battle's tech-by-id table (addresses.BATTLE_TECH_LIST_IDS), so it needs no
    table of which combination techs a party can make -- whatever the game's
    own setup put in the list is what this walks."""
    base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
    ids = bot.machine.peek(A.BATTLE_TECH_LIST_IDS + base, TECHS_A_MEMBER)
    return [(entry, tech_id) for entry, tech_id in enumerate(ids) if tech_id < 0xFB]


def nizbel_ii(bot, state, rng, inventory):
    """Lightning against Nizbel II, with the early party's healing and Ethers.

    His reaction script lowers defence for lightning and raises it for other
    hits; every third lightning hit enables his discharge. Keep the party
    healthy together, and restore MP instead of settling for physical attacks
    against his raised defence. Resolve learned singles by id: this save's
    compact lists put combination techs where later saves have singles.
    """
    member = state.member
    ids = party_ids(bot)
    entries = dict((tech, entry) for entry, tech in tech_list_entries(bot, member))

    def cast(tech, cost, target):
        if tech in entries and state.mp(member) >= cost:
            return ("tech", entries[tech], target)
        return None

    alive = [i for i in range(bot.party) if state.hp(i)]
    for dead in range(bot.party):
        if not state.hp(dead):
            action = use(inventory, REVIVE, dead)
            if action:
                return action
    if not alive:
        return ("attack", None)
    worst = min(alive, key=lambda i: fraction(state, i))
    threshold = 0.65 + 0.1 * rng.random()
    if sum(fraction(state, i) < threshold for i in alive) >= 2:
        action = use(inventory, LAPIS, None)
        if action:
            return action
    if ids[member] in (0, 1) and state.mp(member) < 8:
        for item in (FULL_ETHER, MID_ETHER, ETHER):
            action = use(inventory, item, member)
            if action:
                return action
    if state.max_hp(worst) - state.hp(worst) >= 100:
        # Marle's Cure and Ayla's Kiss are learned here; Cure 2 is not.
        action = cast(12, 2, worst) or cast(41, 1, worst)
        if action:
            return action
        if fraction(state, worst) < 0.4:
            action = use(inventory, MID_TONIC, worst) or use(inventory, LAPIS, None)
            if action:
                return action
    living = enemies(state)
    target = living[0] if living else None
    if ids[member] == 0:
        return cast(5, 8, None) or cast(3, 2, target) or ("attack", target)
    if ids[member] == 1:
        return cast(14, 8, None) or cast(11, 2, target) or ("attack", target)
    return cast(43, 3, target) or ("attack", target)


# Tech ids from here on are double and triple techs (each character's eight single techs come first).
FIRST_COMBINATION_TECH = 57


def tech_sweep(cast_ids, healer_threshold=0.5, self_threshold=0.35, refused=None, finish_with_techs=False,
               combinations_first=False, damage=None):
    """A policy that casts every tech a party's members' lists can show, once
    each: on a member's turn, the next tech list entry whose id isn't in
    `cast_ids` yet (singles before doubles before triples, as the list orders
    them), or an attack once its whole list has been tried. Heals and revives
    with items first, to keep a long fight going. `cast_ids` is the caller's
    set, read fresh every turn, so several members (and a double or triple
    tech's partners) share the one record of what has already been cast; wrap
    `Bot.do` (see tools/movies/routes/tech_sweep.py) to add to it when a tech
    is actually taken, since a dimmed entry here is skipped, not cast.

    `refused`, when given, is a set the same wrap fills with the ids of techs
    the bot tried and couldn't take (not enough MP, a partner's turn not come,
    a revive with nobody down) and empties whenever a tech is taken. Without
    it the first such entry is tried again every turn and blocks the rest of
    its list -- a revive tech ahead of the double techs, which is how the first
    sweep cast none of them. With it the policy goes on to the next entry, and
    tries the refused ones first while an ally is down, when a revive can be
    taken.

    `combinations_first` tries the double and triple techs before the singles:
    they can only be taken while every partner's turn has come, so they are
    the ones a fight that ends early leaves uncast.

    `finish_with_techs` casts a tech already cast where an attack would be the
    fallback: for an enemy only techs hurt (Spekkio's form at the End of Time
    takes nothing from an attack), whose fight would otherwise never end once
    the lists are swept. With `damage` (tech id to the most the enemies' HP
    fell after it, which tracking_do keeps) it takes the strongest of them,
    else one at random.
    """
    def policy(bot, state, rng, inventory):
        member = state.member
        members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
        listed = tech_list_entries(bot, member)
        entries = [(entry, tech_id) for entry, tech_id in listed if tech_id not in cast_ids]
        if combinations_first:
            entries.sort(key=lambda e: e[1] < FIRST_COMBINATION_TECH)
        if refused is not None and any(state.hp(slot) == 0 for slot in members):
            for entry, tech_id in entries:
                if tech_id in refused:
                    refused.discard(tech_id)
                    return ("tech", entry, None)
        action = survival(state, rng, inventory, member, members,
                          healer_threshold=healer_threshold, self_threshold=self_threshold)
        if action:
            return action
        for entry, tech_id in entries:
            if refused is None or tech_id not in refused:
                return ("tech", entry, None)
        if finish_with_techs:
            again = [(entry, tech_id) for entry, tech_id in listed if refused is None or tech_id not in refused]
            if damage:
                strong = sorted(again, key=lambda e: -damage.get(e[1], 0))[:2]
                if strong and damage.get(strong[0][1], 0) > 0:
                    return ("tech", rng.choice(strong)[0], None)
            if again:
                return ("tech", rng.choice(again)[0], None)
        living = enemies(state)
        return ("attack", rng.choice(living) if living else None)
    return policy


def tracking_do(bot, cast_ids, refused=None, damage=None):
    """Wrap `bot.do` so a tech the bot actually takes (not one tried and found
    dimmed) is added to `cast_ids` by id, not by list position: the member and
    the list entry are read before the action runs (`bot.do` moves the menu on
    to the next member once it succeeds), then turned into a tech id through
    the same table `tech_list_entries` reads. With `refused` (tech_sweep's), a
    tech tried and not taken goes there, and the set is emptied once one is.
    With `damage`, a dict, the drop in the enemies' HP from one action to the
    next is credited to the tech taken last (the most seen for each id), for
    tech_sweep's `finish_with_techs`."""
    original = bot.do
    last = []

    def do(action):
        member = bot.state.member
        if damage is not None:
            standing = sum(bot.state.hp(slot) for slot in enemies(bot.state))
            if last:
                tech_id, before = last.pop()
                damage[tech_id] = max(damage.get(tech_id, 0), before - standing)
        tech_id = None
        if action[0] == "tech":
            base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
            tech_id = bot.machine.b(A.BATTLE_TECH_LIST_IDS + base + action[1])
        ok = original(action)
        if tech_id is not None and tech_id < 0xFB:
            if ok:
                cast_ids.add(tech_id)
                if refused is not None:
                    refused.clear()
                if damage is not None:
                    last.append((tech_id, standing))
            elif refused is not None:
                refused.add(tech_id)
        return ok
    bot.do = do


POLICIES = {
    "party": party,
    "party_with_lapis": party_with_lapis,
    "nizbel_ii": nizbel_ii,
    "crono_and_marle": crono_and_marle,
    "crono_and_marle_core": crono_and_marle_core,
}
