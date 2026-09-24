"""Battle menus read from memory: what is on screen, and how to reach a command.

`BattleState` is one frame's worth of the battle addresses in addresses.py, read
in one `runw` round trip. `Bot` turns an action into button presses and verifies
each step by watching the value it should change, so nothing here depends on how
long an animation happens to take:

    ('attack', target)          Attack, then the target cursor
    ('tech', index, target)     the tech list entry `index` (blank rows included)
    ('item', index, target)     the battle item list entry `index`

`target` is a combatant slot (0-2 party, 3-10 enemies) or None to take whatever
the game offers, which is what all-target actions want.
"""
import struct

from . import addresses as A

WATCHES = [
    (A.BATTLE_MENU_MEMBER, 1),
    (A.BATTLE_MENU_PAGE, 1),
    (A.BATTLE_TARGETING, 1),
    (A.BATTLE_COMMAND_CURSOR, 3),
    (A.BATTLE_TECH_CURSOR, 3),
    (A.BATTLE_TECH_SCROLL, 3),
    (A.BATTLE_ITEM_ROW, 2),
    (A.BATTLE_TARGETS, 6),
] + [(A.BATTLE_STATS + slot * A.BATTLE_STAT_STRIDE, 8) for slot in range(11)]

NO_MEMBER = 0x80       # $95D5 >= $80: no command menu is open


class BattleState:
    """The watched bytes of one frame."""

    def __init__(self, frame, data):
        self.frame = frame
        self.member = data[0]
        self.page = data[1]
        self.targeting = data[2]
        self.command_cursor = data[3:6]
        self.tech_cursor = data[6:9]
        self.tech_scroll = data[9:12]
        self.item_row, self.item_top = data[12], data[13]
        self.targets = data[14:20]
        self.stats = [struct.unpack_from("<HHHH", data, 20 + 8 * slot) for slot in range(11)]

    def hp(self, slot):
        return self.stats[slot][0]

    def max_hp(self, slot):
        return self.stats[slot][1]

    def mp(self, slot):
        return self.stats[slot][2]

    def menu_open(self):
        """A command menu is up and taking input (not a list, not targeting)."""
        return self.member < 3 and self.page == 0 and self.targeting == 0


class Bot:
    def __init__(self, machine, party=3, log=None):
        self.machine = machine
        self.party = party
        self.log = log
        machine.watch(WATCHES)
        self.state = self.step(0)

    def say(self, message):
        if self.log:
            self.log(message)

    def step(self, frames=1, buttons=0):
        """Run frames and refresh the battle state."""
        data = self.machine.runw(frames, buttons)
        self.state = BattleState(self.machine.frame, data)
        return self.state

    def inventory(self):
        return (self.machine.peek(A.INVENTORY_ITEMS, A.INVENTORY_SIZE),
                self.machine.peek(A.INVENTORY_COUNTS, A.INVENTORY_SIZE))

    def menu_ready(self):
        return self.state.menu_open()

    # ---- primitives ----------------------------------------------------------------------

    def press(self, buttons, until=None, wait=24, hold=2):
        """Press, then wait for `until(state)` (or a couple of frames)."""
        self.step(hold, buttons)
        for _ in range(wait):
            if until is None or until(self.state):
                return True
            self.step(1)
        return until is None or until(self.state)

    def move_cursor(self, value_of, want, up="UP", down="DOWN", limit=30):
        """Step a cursor to `want`, one press per observed change."""
        for _ in range(limit):
            value = value_of(self.state)
            if value == want:
                return True
            self.press(down if value < want else up, lambda s, v=value: value_of(s) != v, 16)
            self.step(1)
        return value_of(self.state) == want

    def pick_target(self, want):
        """Cycle the target cursor onto slot `want`; a group target is accepted."""
        if want is None:
            return True
        for button in ("RIGHT", "DOWN", "LEFT", "UP"):
            for _ in range(9):
                if self.state.targets[0] == want or self.state.targets[1] != 0xFF:
                    return True
                current = self.state.targets[0]
                self.press(button, lambda s, c=current: s.targets[0] != c, 12)
                self.step(1)
        return self.state.targets[0] == want

    def back_out(self, member):
        for _ in range(4):
            if self.state.member != member or (self.state.page == 0 and self.state.targeting == 0):
                return
            self.press("B", None, 10)

    # ---- actions -------------------------------------------------------------------------

    def do(self, action):
        """Carry out an action. True when the command was accepted."""
        member = self.state.member
        kind = action[0]
        want_command = {"attack": 0, "tech": 1, "item": 2}[kind]
        self.step(4)
        if not self.move_cursor(lambda s: s.command_cursor[member] if s.member == member else -1, want_command):
            return False
        if kind == "attack":
            if not self.press("A", lambda s: s.targeting != 0 or s.member != member):
                return False
            self.step(1)
            self.pick_target(action[1])
        else:
            page = 1 if kind == "tech" else 2
            if not self.press("A", lambda s: s.page == page or s.member != member):
                # the menu can still be setting its cursor up when it first
                # shows (the member's remembered command), so A may open
                # another list: back out of it and try again
                self.back_out(member)
                return False
            self.step(10)      # the list window takes a few frames before it accepts input
            if kind == "tech":
                def index(s):
                    if s.member != member or s.page != 1:
                        return -1
                    return s.tech_scroll[member] + s.tech_cursor[member]
            else:
                def index(s):
                    if s.member != member or s.page != 2:
                        return -1
                    return s.item_top + s.item_row
            if not self.move_cursor(index, action[1]):
                self.back_out(member)
                return False
            self.step(1)
            took = False
            for _ in range(3):
                if self.press("A", lambda s: s.targeting != 0 or s.member != member, 12):
                    took = True
                    break
                if self.state.member != member or self.state.page != page:
                    break
            if not took:
                self.back_out(member)
                return False
            self.step(1)
            if self.state.member == member and self.state.targeting:
                self.pick_target(action[2])
        if self.state.member != member:
            return True
        self.press("A", lambda s: s.member != member or s.targeting == 0, 20)
        for _ in range(30):
            if self.state.member != member or self.state.targeting == 0:
                break
            self.step(1)
        if self.state.member == member:
            self.back_out(member)
            return False
        return True
