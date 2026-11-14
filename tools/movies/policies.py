"""What a character should do, given the battle state.

A policy is `f(bot, state, rng, inventory) -> action` for `Bot.do`. They are
deliberately loose: every choice is a coin flip around a threshold, so the route
runner's backtracking (campaign.Run.fight) can retry a losing fight with other
choices instead of needing a policy that is right the first time.

Item indices and available counts come from the battle item list, tech indices
from the in-battle tech list, so a policy never assumes a fixed menu layout.
"""
from . import addresses as A

CRONO, ROBO = 0, 3                       # character numbers

# Consumables, in inventory order ($BD-$C7).
TONIC, MID_TONIC, FULL_TONIC, ETHER, MID_ETHER, FULL_ETHER, ELIXIR, HYPER_ETHER, MEGA_ELIXIR, HEAL, REVIVE = \
    range(0xBD, 0xC8)

# Tech list entries per character id, as index and MP cost:
#   revive, heal_all, heal_one, all (hit every enemy), one (hit one enemy)
# The lists are tried in order and skipped when the MP isn't there or the entry
# isn't learned yet (tech_known); attacking is always the fallback.
CHARACTER_TECHS = {
    0: dict(revive=(5, 10), all=[(7, 20), (4, 15)], one=[(2, 2), (1, 2)]),                      # Crono
    1: dict(revive=(7, 15), heal_one=(6, 5), one=[(5, 8), (2, 2)]),                             # Marle
    2: dict(all=[(7, 20), (6, 15), (3, 3)], one=[(5, 8), (2, 2)]),                              # Lucca
    3: dict(heal_all=(4, 3), heal_one=(1, 2), all=[(6, 14), (2, 3)], one=[(5, 12), (0, 1)]),    # Robo
    4: dict(heal_all=(3, 2), heal_one=(6, 5)),                                                  # Frog
    5: dict(),                                                                                  # Ayla
    6: dict(all=[(7, 20), (5, 10)], one=[(0, 8), (1, 8), (2, 8)]),                              # Magus
}


def item_index(inventory, item):
    """The menu row holding an available `item`, or None when there is none."""
    items, counts = inventory
    for slot in range(len(items)):
        if items[slot] == item and counts[slot] > 0:
            return slot
    return None


def use(inventory, item, target):
    index = item_index(inventory, item)
    return None if index is None else ("item", index, target)


def tech_known(bot, member, index):
    """False when list entry `index` is a blank row (see addresses.BATTLE_TECH_LIST)."""
    base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
    return bot.machine.b(A.BATTLE_TECH_LIST + base + index) != 0xFF


def single_tech_known(bot, member, index):
    """Whether list entry `index` is one of the member's single techs: every
    row up to it holds a tech. The single techs come first; the double and
    triple techs follow a blank row, so a single tech's number past the ones
    learned would name a double tech there."""
    base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
    return all(bot.machine.b(A.BATTLE_TECH_LIST + base + i) != 0xFF for i in range(index + 1))


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


def party_lavos_core(bot, state, rng, inventory):
    """Keep party's healing; clear the left bit with physical attacks first.

    Then use the strongest known area tech on the body and exposed core.
    Robo's Shock (entry 7, 17 MP) reaches them both; his generic policy's
    Area Bomb depends on where he stands. Restore MP for these short windows.
    """
    action = party(bot, state, rng, inventory)
    character = party_ids(bot)[state.member]
    techs = CHARACTER_TECHS.get(character, {})
    offensive = {index for index, _ in techs.get("all", []) + techs.get("one", [])}
    if action[0] != "attack" and not (action[0] == "tech" and action[1] in offensive):
        return action
    if state.hp(7) > 0:
        return ("attack", 7)
    area = (7, 17) if character == 3 else next(iter(techs.get("all", [])), None)
    if area and tech_known(bot, state.member, area[0]):
        if state.mp(state.member) >= area[1]:
            return ("tech", area[0], None)
        ether = use(inventory, HYPER_ETHER, state.member)
        if ether:
            return ether
    target = 3 if state.hp(3) > 0 else 9
    if action[0] == "attack":
        return ("attack", target)
    if action[2] is not None:
        return ("tech", action[1], target)
    return action


LAPIS = 0xCA


def motherbrain(bot, state, rng, inventory):
    """Clear the Displays and finish before MotherBrain's repeated stat gains.

    Her action $15 makes a long fight increasingly expensive. Prefer the
    party's magic each turn over the general policy's random physical attacks.
    Robo supplies group healing; individual healing is reserved for low HP.
    """
    member = state.member
    character = party_ids(bot)[member]
    members = range(bot.party)
    dead = [i for i in members if not state.hp(i)]
    if dead:
        if character == 1 and state.mp(member) >= 15 and tech_known(bot, member, 7):
            return ("tech", 7, dead[0])
        action = use(inventory, REVIVE, dead[0])
        if action:
            return action
    alive = [i for i in members if state.hp(i)]
    low = [i for i in alive if fraction(state, i) < 0.5]
    if character == 3 and len(low) >= 2 and state.mp(member) >= 3 and tech_known(bot, member, 4):
        return ("tech", 4, None)
    if alive:
        lowest = min(alive, key=lambda i: fraction(state, i))
        if fraction(state, lowest) < 0.25:
            if character == 1 and state.mp(member) >= 5 and tech_known(bot, member, 6):
                return ("tech", 6, lowest)
            action = use(inventory, FULL_TONIC, lowest)
            if action:
                return action
    spells = {1: [(5, 8), (2, 2)], 2: [(7, 20), (5, 8), (2, 2)],
              3: [(7, 17), (2, 3)]}.get(character, [])
    for index, cost in spells:
        if state.mp(member) >= cost and tech_known(bot, member, index):
            return ("tech", index, None)
    action = use(inventory, HYPER_ETHER, member)
    return action or ("attack", (enemies(state) or [3])[0])


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


def party_with_ethers(bot, state, rng, inventory):
    """`party_with_lapis` for a long fight, such as Lavos's shell and its
    imitations one after another: a member short of MP takes an Ether of the
    bag (the Full, the Mid, the plain), which `party` would look for only as
    Hyper Ethers."""
    member = state.member
    members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
    if all(state.hp(slot) for slot in members) and state.mp(member) < 10 and rng.random() < 0.4:
        for item in (FULL_ETHER, MID_ETHER, ETHER):
            action = use(inventory, item, member)
            if action:
                return action
    return party_with_lapis(bot, state, rng, inventory)


MID_TONIC_GAP = 300          # missing HP a Mid Tonic is enough for


def keep_up(bot, state, inventory, well=0.6, badly=0.35):
    """The healing a long fight against an enemy that hits the whole party
    hard puts first, or None: the fallen raised (a member's Life where it
    has one, else a Revive); two or more hurt
    (or one badly) healed, by Robo's Heal Beam or Marle's Cure 2 where they
    have the MP, else a Lapis for two or more, a Mid Tonic (some 200 HP) for
    one missing less than MID_TONIC_GAP, a Full Tonic for more; Robo short of
    MP drinks an Ether. The techs come first: a long run of fights (Lavos's
    shell, its inner form and its core one after another) would otherwise
    empty the bag before the last."""
    member = state.member
    members = [slot for slot in range(bot.party) if state.max_hp(slot) > 0]
    ids = party_ids(bot)
    alive = [slot for slot in members if state.hp(slot) > 0]
    revive = CHARACTER_TECHS.get(ids[member], {}).get("revive")
    for slot in members:
        if state.hp(slot) == 0:
            if revive and state.mp(member) >= revive[1] and tech_known(bot, member, revive[0]):
                return ("tech", revive[0], slot)
            action = use(inventory, REVIVE, slot)
            if action:
                return action
    hurt = [slot for slot in alive if fraction(state, slot) < well]
    if len(hurt) >= 2 or any(fraction(state, slot) < badly for slot in alive):
        heal_all = CHARACTER_TECHS.get(ids[member], {}).get("heal_all")
        if heal_all and len(hurt) >= 2 and state.mp(member) >= heal_all[1] and tech_known(bot, member, heal_all[0]):
            return ("tech", heal_all[0], None)
        worst = min(hurt or alive, key=lambda slot: fraction(state, slot))
        heal_one = CHARACTER_TECHS.get(ids[member], {}).get("heal_one")
        if heal_one and state.mp(member) >= heal_one[1] and tech_known(bot, member, heal_one[0]):
            return ("tech", heal_one[0], worst)
        tonics = ((MID_TONIC, worst), (FULL_TONIC, worst)) if state.max_hp(worst) - state.hp(worst) < MID_TONIC_GAP             else ((FULL_TONIC, worst), (MID_TONIC, worst))
        items = ((LAPIS, None),) + tonics if len(hurt) >= 2 else tonics
        for item, target in items:
            action = use(inventory, item, target)
            if action:
                return action
    if state.mp(member) < 5 and ids[member] == ROBO:
        for item in (FULL_ETHER, MID_ETHER, ETHER):
            action = use(inventory, item, member)
            if action:
                return action
    return None


def holding_out(bot, state, rng, inventory):
    """For a long fight against an enemy that hits the whole party hard each
    turn (Lavos's shell): keep everyone up first (keep_up), then attack the
    weakest enemy, whose defence a weapon still gets through where most magic
    barely does."""
    action = keep_up(bot, state, inventory)
    if action:
        return action
    living = enemies(state)
    return ("attack", min(living, key=lambda slot: state.hp(slot)) if living else None)


INNER_ARMS, INNER_BODY = (3, 6), 10     # the inner Lavos's arms (8000 and 12000 HP) and body (20000)
BODY_LAST_BLOWS = 4000                  # the body's HP from which the party readies for the core first
# The guards a member wears through a fight: the Barrier (0xCB) takes three
# tenths off magic, the Shield (0xCC) and Lucca's Protect (her tech list's
# entry 4) off blows; one member each. Their bits in the stat block's fourth
# status byte.
BARRIER, SHIELD = 0xCB, 0xCC
STATUS_4, BARRIERED, SHIELDED = 0x21 - 0x03, 0x40, 0x04
LUCCA, PROTECT, PROTECT_MP = 2, 4, 6


def guard_up(bot, state, inventory):
    """A guard against blows on the first member standing without one (Lucca's
    Protect while she has the MP, else a Shield), then a Barrier against
    magic, or None: the core's center strikes everyone at once with a blow
    of 240 power on its attack of 100, which a member with Lucca's defence
    does not live through unguarded."""
    member = state.member
    members = [slot for slot in range(bot.party) if state.hp(slot) > 0]
    guarded = lambda slot, bit: bot.machine.b(A.BATTLE_STATS + slot * A.BATTLE_STAT_STRIDE + STATUS_4) & bit
    for slot in members:
        if not guarded(slot, SHIELDED):
            if party_ids(bot)[member] == LUCCA and state.mp(member) >= PROTECT_MP and tech_known(bot, member, PROTECT):
                return ("tech", PROTECT, slot)
            action = use(inventory, SHIELD, slot)
            if action:
                return action
    for slot in members:
        if not guarded(slot, BARRIERED):
            action = use(inventory, BARRIER, slot)
            if action:
                return action
    return None


def refill(bot, state, inventory, low=20):
    """An Ether (the Mid, then the plain) for a member short of the MP its
    techs want, or None."""
    member = state.member
    if state.mp(member) >= low or not CHARACTER_TECHS.get(party_ids(bot)[member]):
        return None
    for item in (MID_ETHER, ETHER):
        action = use(inventory, item, member)
        if action:
            return action
    return None


# Against Lavos, each member's strongest single-target techs first, as
# (tech list entry, MP): Crono's Confuse (four blows) and Spincut, Marle's
# Ice 2, Lucca's Flare and Fire 2, Robo's Uzzi Punch and Rocket Punch, Frog's
# Leap Slash and Slurp Cut. Blows only, for the core's left bit, which takes
# in magic (its elements' affinities absorb all four).
LAVOS_SINGLES = {0: [(6, 12), (3, 4)], 1: [(5, 8)], 2: [(7, 20), (5, 8)], 3: [(5, 12), (0, 1)], 4: [(4, 4), (1, 2)]}
LAVOS_BLOWS = {0: [(6, 12), (3, 4)], 3: [(5, 12), (0, 1)], 4: [(4, 4), (1, 2)]}
# Crono, Frog and Robo's Triple Raid (tech 109), three blows together: the
# guides' answer to the core's left bit and to the right bit's open moments
# (cavesofnarshe.com's walkthrough, chapter 35). Each member pays its part;
# TRIPLE_RAID_MP is a margin above the dearest part.
TRIPLE_RAID, TRIPLE_RAID_MP = 109, 12


TRIPLE_WAIT = 30                # frames a member's menu is held open for the others' turns


LUMINAIRE, LUMINAIRE_MP = 7, 20      # Crono's tech list entry for his spell on every enemy, and its cost


def luminaire(bot, state):
    """Crono's Luminaire on every enemy, where he knows it and has the MP, or None."""
    member = state.member
    if party_ids(bot)[member] != CRONO or state.mp(member) < LUMINAIRE_MP or not single_tech_known(bot, member, LUMINAIRE):
        return None
    return ("tech", LUMINAIRE, None)


def triple_raid(bot, state, rng, target):
    """Triple Raid at `target` from the member's tech list while all three
    stand with the MP, or None. It takes all three members' turns at once:
    where the list showed it dimmed when it last opened (the others' turns
    not yet come), the member waits with its menu open about half the time
    rather than trying again at once."""
    if any(state.hp(slot) == 0 or state.mp(slot) < TRIPLE_RAID_MP for slot in range(bot.party)):
        return None
    for entry, tech_id in tech_list_entries(bot, state.member):
        if tech_id == TRIPLE_RAID:
            if A.battle_tech_dimmed(bot.machine, state.member, entry) and rng.random() < 0.5:
                return ("wait", TRIPLE_WAIT)
            return ("tech", entry, target)
    return None


STAT_STATUS_1, STOPPED = 0x1E, 0x80               # the first status byte in the stat block, Stop's bit


def unstop(bot, state, inventory):
    """A Heal for a member the core's spell has stopped, or None: a stopped
    member's turn never comes, and the others' alone cannot hold the party up."""
    for slot in range(bot.party):
        if state.hp(slot) > 0 and slot != state.member and stat(bot, slot, STAT_STATUS_1) & STOPPED:
            action = use(inventory, HEAL, slot)
            if action:
                return action
    return None


def haste_fuel(bot, state, inventory):
    """An Ether for Marle while she lacks the MP for Haste and someone is not
    yet hasted, or None."""
    member = state.member
    if party_ids(bot)[member] != MARLE or state.mp(member) >= HASTE_MP or not tech_known(bot, member, HASTE):
        return None
    if all(bot.machine.b(A.BATTLE_STATS + slot * A.BATTLE_STAT_STRIDE + STATUS_3) & HASTED
           for slot in range(bot.party) if state.hp(slot) > 0):
        return None
    return refill(bot, state, inventory, low=HASTE_MP)


def strongest_single(bot, state, target, table=None):
    """The member's strongest single-target tech it has the MP for, at
    `target` (from `table` where given, else CHARACTER_TECHS' singles), or an
    attack."""
    member = state.member
    character = party_ids(bot)[member]
    singles = table.get(character, []) if table is not None else CHARACTER_TECHS.get(character, {}).get("one", [])
    for index, cost in singles:
        if state.mp(member) >= cost and single_tech_known(bot, member, index):
            return ("tech", index, target)
    return ("attack", target)


def lavos_inner(bot, state, rng, inventory):
    """Lavos's inner form. Before anyone acts an arm's spell hits each member
    once: 37 power on its level and magic (50 and 50, read from its stat
    block, $C1:E0F5), 2,960 before magic defence ($C1:E65F) and 0-16 of
    variance. Computed so, a member at full HP needs about 70 magic defence
    (69 at 999 HP, 73 at 891) to live through it; Robo at 67 does not (1,029
    in ct_native), at 73 he does. Its body's defence turns
    weapons and the arms raise it again, so the arms go first and together,
    the one with more HP left taken down (cavesofnarshe.com's walkthrough
    chapter 35, and gamercorner.net's: Span Death revives the body while an
    arm stands); then the body. Crono's Luminaire hits all three at once;
    Triple Raid where the party has it.
    Healing first (the healers' techs before the bag), Marle's Haste where
    she is in the party, an Ether for a member short of MP, then each member's
    strongest single hit (LAVOS_SINGLES). The bag's Barriers and Shields are
    kept for the core."""
    action = keep_up(bot, state, inventory) or hasten(bot, state) or refill(bot, state, inventory, low=8)
    if action:
        return action
    arms = [slot for slot in INNER_ARMS if state.hp(slot) > 0]
    if not arms and state.hp(INNER_BODY) <= BODY_LAST_BLOWS:
        # The core's fight follows at once, with no field between: its
        # opening rounds (spells on everyone, Stop and Confusion) take some
        # 2,000 HP from the party before it acts, so it goes in whole.
        action = keep_up(bot, state, inventory, well=0.95, badly=0.9) or refill(
            bot, state, inventory, low=state.stats[state.member][3] * 4 // 5)
        if action:
            return action
    target = max(arms, key=lambda slot: state.hp(slot)) if arms else INNER_BODY
    return (luminaire(bot, state) or triple_raid(bot, state, rng, target)
            or strongest_single(bot, state, target, LAVOS_SINGLES))


# Lavos's core: its center (slot 3, 10000 HP), left bit (7, 2000) and right
# bit (9, 30000), the core itself, whose fall ends the fight.
CORE_CENTER, CORE_LEFT, CORE_RIGHT = 3, 7, 9
STAT_DEFENCE, STAT_MAGIC_DEFENCE = 0x3E, 0x3C    # offsets in a combatant's stat block
CORE_GUARDED = 0xFF                              # the right bit's defence while it is shut


def stat(bot, slot, offset):
    """A byte of combatant `slot`'s stat block (whose HP word BATTLE_STATS names, three in)."""
    return bot.machine.b(A.BATTLE_STATS - 3 + slot * A.BATTLE_STAT_STRIDE + offset)


def lavos_core(bot, state, rng, inventory):
    """Lavos's core. Its right bit is the core: its defence stands at the most
    while the others stand, so a blow does 1. The left bit (which absorbs
    magic and heals the core) goes first, to weapons; then the center. When
    one falls the right bit lowers its guard to bring it back: the defence
    read from its stat block below the most is the window to hit it with
    everything. The center's Evil Star halves everyone and Grand Stone and
    Crying Heavens hit all, and the center strikes everyone at once (2,666
    before defence), so healing comes first, then a Heal for a member its
    spell has stopped (the stat block's Stop bit), Marle's Haste, and guards
    on everyone (guard_up). Some choices are left to chance (whether to guard
    or top up first, the left bit or the center) so the bot's retries can
    find a line through.
    (Gamercorner.net's and Neoseeker's core guides; the defence read is the
    port's own record.)"""
    action = (keep_up(bot, state, inventory, well=0.4, badly=0.3) or unstop(bot, state, inventory)
              or hasten(bot, state) or haste_fuel(bot, state, inventory)
              or (rng.random() < 0.8 and guard_up(bot, state, inventory))
              or (rng.random() < 0.7 and keep_up(bot, state, inventory, well=0.7, badly=0.55))
              or refill(bot, state, inventory))
    if action:
        return action
    if state.hp(CORE_RIGHT) > 0 and stat(bot, CORE_RIGHT, STAT_DEFENCE) < CORE_GUARDED:
        return triple_raid(bot, state, rng, CORE_RIGHT) or strongest_single(bot, state, CORE_RIGHT, LAVOS_BLOWS)
    if state.hp(CORE_LEFT) > 0 and (state.hp(CORE_CENTER) == 0 or rng.random() < 0.7):
        return triple_raid(bot, state, rng, CORE_LEFT) or strongest_single(bot, state, CORE_LEFT, LAVOS_BLOWS)
    if state.hp(CORE_LEFT) == 0:
        # With the left bit down nothing takes the spell in: Luminaire on all.
        action = luminaire(bot, state)
        if action:
            return action
    if state.hp(CORE_CENTER) > 0:
        # Plain blows on the center until the right bit opens (chapter 35):
        # the members keep their MP for the moments that count.
        return ("attack", CORE_CENTER)
    return strongest_single(bot, state, CORE_RIGHT, LAVOS_SINGLES)


LAVOS_SHELL, LAVOS_SHELL_HP = 3, 10000
MARLE, HASTE, HASTE_MP = 1, 4, 6        # Marle's tech list entry and its cost
SHOCK, SHOCK_MP = 7, 17                 # Robo's lightning on all, which takes Nizbel's guard down
NIZBEL_IMITATION_HP = 4200
STATUS_3, HASTED = 0x20 - 0x03, 0x80    # the stat block's third status byte (from its HP word), haste's bit


def hasten(bot, state):
    """Marle's Haste on the first member of the party not yet hasted (the
    others before herself), while she has the MP: everyone's turns come
    twice as fast."""
    member = state.member
    if party_ids(bot)[member] != MARLE or state.mp(member) < HASTE_MP or not tech_known(bot, member, HASTE):
        return None
    slots = [slot for slot in range(bot.party) if slot != member] + [member]
    for slot in slots:
        hasted = bot.machine.b(A.BATTLE_STATS + slot * A.BATTLE_STAT_STRIDE + STATUS_3) & HASTED
        if state.hp(slot) > 0 and not hasted:
            return ("tech", HASTE, slot)
    return None


def lavos_shell(bot, state, rng, inventory):
    """Lavos's shell on the Day of Lavos: its imitations of the earlier bosses
    with `party_with_ethers` (their tricks want the members' techs), then the
    shell's own form, which hits everyone hard each turn, with `holding_out`."""
    if enemies(state) == [LAVOS_SHELL] and state.max_hp(LAVOS_SHELL) == LAVOS_SHELL_HP:
        haste = hasten(bot, state)
        return haste or holding_out(bot, state, rng, inventory)
    action = party_with_ethers(bot, state, rng, inventory)
    member = state.member
    nizbel = enemies(state) == [LAVOS_SHELL] and state.max_hp(LAVOS_SHELL) == NIZBEL_IMITATION_HP
    if nizbel and party_ids(bot)[member] == ROBO and action[0] in ("attack", "tech") and             state.mp(member) >= SHOCK_MP and tech_known(bot, member, SHOCK):
        return ("tech", SHOCK, None)
    return action


def r_series(bot, state, rng, inventory):
    """Crono and Lucca against the factory's two columns of R Series robots.

    Their middle slots (4 and 9) center Fire Whirl on a column. Read its
    entry from the live list: combination techs follow the learned singles,
    so they do not have a fixed menu index. The early save has Tonics and
    Mid Tonics, but none of the late healing items the general policy uses.
    """
    member = state.member
    for slot in range(2):
        if not state.hp(slot):
            action = use(inventory, REVIVE, slot)
            if action:
                return action
    lowest = min(range(2), key=lambda slot: fraction(state, slot))
    if fraction(state, lowest) < 0.5 + 0.15 * rng.random():
        for item in (MID_TONIC, TONIC):
            action = use(inventory, item, lowest)
            if action:
                return action
    living = enemies(state)
    if not living:
        return ("attack", None)
    target = next((slot for slot in (4, 9) if slot in living), living[0])
    if state.mp(0) >= 2 and state.mp(1) >= 3 and rng.random() < 0.9:
        base = bot.machine.b(A.BATTLE_TECH_LIST_BASE + member)
        entries = bot.machine.peek(A.BATTLE_TECH_LIST_IDS + base, 20)
        if 0x3C in entries:                         # Fire Whirl
            return ("tech", entries.index(0x3C), target)
    if member == 0 and state.mp(0) >= 2:
        return ("tech", 0, target)                  # Cyclone
    return ("attack", min(living, key=lambda slot: state.hp(slot)))


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
    "party_lavos_core": party_lavos_core,
    "party_with_lapis": party_with_lapis,
    "party_with_ethers": party_with_ethers,
    "holding_out": holding_out,
    "lavos_shell": lavos_shell,
    "lavos_inner": lavos_inner,
    "lavos_core": lavos_core,
    "r_series": r_series,
    "motherbrain": motherbrain,
    "nizbel_ii": nizbel_ii,
    "crono_and_marle": crono_and_marle,
    "crono_and_marle_core": crono_and_marle_core,
}
