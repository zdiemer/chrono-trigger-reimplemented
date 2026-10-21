"""Sweep the field menu's screens on the 1000 A.D. world map, for the paths no other movie takes.

Save: ctsave14 slot 1 (fantasyanime archive; the party of seven stands on the 1000 A.D. world map, $1F0).

From the main menu: the items screen (a Shelter and a Lapis tried on the party, whose cursor
covers every member, a weapon's users shown, the list paged and a Power Tab used on a member,
looking at each in turn), the techs screen (the members' list slid past its three rows both
ways, Crono's double techs scrolled past their eight rows and paged), the equipment screen (the
members' list slid down to a reserve member, a weapon changed, the shoulder buttons switching
member to member from the slots), the settings (the window design stepped along its meter and
put back, the controls set to the player's own with two assignments changed, then Start putting
the game's buttons back), where it stops: closing the menu reaches nothing new.

The party is at full health on this save, so the Shelter's rest and the Lapis's healing are
refused after their member windows open; what they reach is the window and the cursors.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Sweep the field menu on the 1000 A.D. world map: items (a Shelter and a Lapis tried on the party, a weapon's users
# shown, the list paged and a Power Tab used, looking at each member), techs (the members' list slid both ways past its
# three rows, Crono's double techs scrolled past eight rows and paged), equipment (the members slid to a reserve member, a
# weapon changed, the shoulder buttons switching member from the slots), settings (the window design stepped and put
# back, the controls set to custom with two assignments changed, Start putting the game's buttons back), and out of the
# controls' rows (route menu_sweep).
# Save: ctsave14 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave14-side_quests3.srm"
SLOT = 0
PHASES = ["loaded", "items", "techs", "equipment", "settings"]

MENU_SCREEN = 0x7E0067          # menu_screen: 0 the main menu, 1 equipment, 2 items, 3 techs, 4 settings
CURSOR = 0x7E0054               # menu_cursor_index
ITEM_LIST_TOP = 0x7E0416        # menu_item_list_top: the first place of the party's items the list shows
INVENTORY_ITEMS, INVENTORY_COUNTS = 0x7E2400, 0x7E2500
MAIN_MENU, EQUIPMENT, ITEMS, TECHS, SETTINGS = 0, 1, 2, 3, 4
OPTION_OF = {EQUIPMENT: 0, ITEMS: 1, TECHS: 2, SETTINGS: 3}
SHELTER, LAPIS, POWER_TAB = 0xC8, 0xCA, 0xCD
SETTINGS_WINDOW_DESIGN, SETTINGS_CONTROLS = 6, 8


def press(machine, button, gap=25, hold=3):
    machine.run(hold, button)
    machine.run(gap)


def wait_for(machine, address, value, frames=300):
    for _ in range(frames):
        if machine.b(address) == value:
            return True
        machine.run(1)
    return machine.b(address) == value


def open_menu(machine):
    press(machine, "X", 60)
    assert wait_for(machine, MENU_SCREEN, MAIN_MENU), "the main menu opens"


def open_option(machine, screen):
    """From the main menu, move to an option's icon and open its screen."""
    for _ in range(8):
        if machine.b(CURSOR) == OPTION_OF[screen]:
            break
        press(machine, "RIGHT" if machine.b(CURSOR) < OPTION_OF[screen] else "LEFT")
    assert machine.b(CURSOR) == OPTION_OF[screen], "the cursor on the option"
    press(machine, "A", 40)
    assert wait_for(machine, MENU_SCREEN, screen), "screen %d opens" % screen


def back_to_main_menu(machine):
    for _ in range(6):
        if machine.b(MENU_SCREEN) == MAIN_MENU:
            break
        press(machine, "B", 30)
    assert wait_for(machine, MENU_SCREEN, MAIN_MENU), "back on the main menu"


def item_under_cursor(machine):
    return machine.b(INVENTORY_ITEMS + machine.b(ITEM_LIST_TOP) + machine.b(CURSOR))


def count_of(machine, item):
    """How many of `item` the party has: 0 once the last is used, its place then emptied."""
    items = machine.peek(INVENTORY_ITEMS, 0xF2)
    return machine.b(INVENTORY_COUNTS + items.index(bytes([item]))) if bytes([item]) in items else 0


def to_item(run, item):
    """Move the items list's cursor onto `item`: page down with L (the shoulder that pages down here)
    while the item lies past the rows shown, then a row at a time."""
    machine = run.machine
    place = machine.peek(INVENTORY_ITEMS, 0xF2).index(bytes([item]))
    for _ in range(30):
        if machine.b(ITEM_LIST_TOP) + 9 >= place:
            break
        press(machine, "L", 30)
    for _ in range(40):
        here = machine.b(ITEM_LIST_TOP) + machine.b(CURSOR)
        if here == place:
            break
        press(machine, "DOWN" if here < place else "UP")
    assert item_under_cursor(machine) == item, "the cursor on item %02X" % item


def use_item(run, item, choose_member=None, extra=()):
    """Pick an item up and use it: its member window opens; optionally move the cursor about and
    choose, then go back out of the window."""
    machine = run.machine
    to_item(run, item)
    press(machine, "A", 30)             # picked up
    press(machine, "A", 50)             # used: the member window
    for button in extra:
        press(machine, button, 30)
    if choose_member is not None:
        press(machine, "A", 90)
    press(machine, "B", 40)             # the window closed


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        run.select_slot(SLOT)
        machine.run(4, "A")
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        assert run.map_id() == "f001", "the 1000 A.D. world map"
        run.checkpoint("loaded")

    if start <= 1:
        open_menu(machine)
        open_option(machine, ITEMS)
        machine.run(20)
        use_item(run, SHELTER)                           # the party's rest: refused at full health
        use_item(run, LAPIS)                             # every member's cursor: refused at full health
        weapon = machine.b(INVENTORY_ITEMS + 22)         # the first weapon in the list: its users shown
        to_item(run, weapon)
        press(machine, "A", 30)
        press(machine, "A", 60)
        press(machine, "B", 40)
        tabs = count_of(machine, POWER_TAB)
        use_item(run, POWER_TAB, choose_member=True, extra=("DOWN", "DOWN", "UP"))
        used = tabs - count_of(machine, POWER_TAB)
        run.say("power tabs used", used, "frame", machine.frame)
        assert used == 1, "a Power Tab used"
        back_to_main_menu(machine)
        run.checkpoint("items")

    if start <= 2:
        open_option(machine, TECHS)
        machine.run(20)
        for button in ["DOWN"] * 5 + ["UP"] * 5:         # the members' list slides past its three rows
            press(machine, button)
        press(machine, "RIGHT", 40)                       # Crono's double techs
        press(machine, "A", 40)
        for _ in range(10):                               # past the list's eight rows
            press(machine, "DOWN")
        press(machine, "R", 40)
        press(machine, "L", 40)
        back_to_main_menu(machine)
        run.checkpoint("techs")

    if start <= 3:
        open_option(machine, EQUIPMENT)
        machine.run(20)
        for _ in range(4):                                # the members slide to the reserve
            press(machine, "DOWN", 40)
        press(machine, "A", 40)                           # the member's slots
        press(machine, "A", 40)                           # the weapon's list
        press(machine, "DOWN", 30)
        press(machine, "A", 60)                           # another weapon on
        press(machine, "R", 60)                           # the next member, from the slots
        press(machine, "R", 60)
        press(machine, "L", 60)
        back_to_main_menu(machine)
        run.checkpoint("equipment")

    if start <= 4:
        open_option(machine, SETTINGS)
        machine.run(30)
        for button in ("RIGHT", "DOWN"):                  # the window design
            press(machine, button)
        assert machine.b(CURSOR) == SETTINGS_WINDOW_DESIGN, "the cursor on the window design"
        press(machine, "A", 40)
        for button in ("RIGHT", "RIGHT", "LEFT", "LEFT"):
            press(machine, button, 40)
        press(machine, "A", 40)
        press(machine, "DOWN")
        press(machine, "DOWN")
        assert machine.b(CURSOR) == SETTINGS_CONTROLS, "the cursor on the controls"
        press(machine, "A", 40)                           # the player's own buttons: the rows
        press(machine, "RIGHT", 30)                       # confirm's button stepped on
        press(machine, "DOWN", 30)                        # kept
        press(machine, "RIGHT", 30)
        press(machine, "RIGHT", 30)
        press(machine, "START", 30)                       # the game's own buttons back
        press(machine, "LEFT", 30)                        # out of the rows
        assert machine.b(CURSOR) == SETTINGS_CONTROLS, "back on the controls"
        run.checkpoint("settings")

    run.say("end frame", machine.frame)
    run.finish("the menu sweep")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
