"""Empty Marle's armor and helmet with the menu glitch, then fight Spekkio.

ctsave04 slot 2 has no spare armor or helmets. Switching member and slot
together preserves the equipment list's member-plus-slot cache key. A stale
accessory list lets Marle choose item zero for her armor; that returned armor
then supplies the stale list for her helmet. See docs/movies-wanted.md, Target 14.
"""
import json

from .. import policies
from ..campaign import Run, route_arguments, run_route
from . import menu_sweep as menu
from . import tech_sweep_spekkio as spekkio

SAVE = "oracle/saves/fantasyanime/ctsave04-masamune.srm"
HEADER = """\
# Unequip glitch: from Denadoro's world-map save, leave Marle's armor and helmet empty with R+Up in the equipment
# screen. Cross Zenan Bridge and Truce Canyon to the End of Time, then cast Ice and take Spekkio's elemental magic.
# End with the field back in hand (route unequipped_armor).
# Save: ctsave04 slot 2 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

MARLE_GEAR = 0x7E2677             # character 1's helmet, armor, weapon, accessory
MENU_MEMBER = 0x7E95D5


def unequip_marle(machine):
    menu.open_menu(machine)
    # Visit an empty item row first. The save leaves item_operand at $F1;
    # the original preserves it when the first equipment list has no items,
    # while the existing port clears it. Normal cursor movement leaves zero
    # on both sides, avoiding that unrelated scratch-byte mismatch.
    menu.open_option(machine, menu.ITEMS)
    for _ in range(40):
        if menu.item_under_cursor(machine) == 0:
            break
        menu.press(machine, "DOWN")
    assert menu.item_under_cursor(machine) == 0, "an empty inventory row"
    menu.back_to_main_menu(machine)
    menu.open_option(machine, menu.EQUIPMENT)
    menu.press(machine, "A", 40)
    for _ in range(3):
        menu.press(machine, "DOWN")
    menu.press(machine, "R+UP", 60)  # Crono/accessory -> Marle/armor, same sum
    menu.press(machine, "A", 60)
    menu.press(machine, "A", 60)
    assert machine.b(MARLE_GEAR + 1) == 0, "Marle's armor emptied"
    menu.press(machine, "L", 60)     # Crono/armor: Marle's returned armor fits
    menu.press(machine, "R+UP", 60)  # -> Marle/helmet, same sum again
    menu.press(machine, "A", 60)
    menu.press(machine, "A", 60)
    assert machine.peek(MARLE_GEAR, 2) == b"\0\0", "both slots emptied"
    menu.back_to_main_menu(machine)
    menu.press(machine, "B", 100)


def small_fight(run):
    def attack(bot, state, rng, inventory):
        living = policies.enemies(state)
        return "attack", living[0] if living else None
    run.fight(attack, lambda r, s: not policies.enemies(s), party=3)
    assert run.wait_control(), "the canyon back in hand"


def fight_spekkio(run, seed=1):
    """Marle casts Ice; her companions keep the party alive with Mid Tonics.

    Observe HP changes at the bot's steps, including those inside menu actions.
    The displayed number is kept separately from HP lost (a finishing hit can
    exceed the target's remaining HP). These are read-only observations.
    """
    machine = run.machine
    assert run.go(*spekkio.SPEKKIO_SPOT, tolerance=3), "beside Spekkio"
    machine.run(2, "DOWN")
    machine.run(8)
    for attempt in range(80):
        if machine.w(spekkio.ENEMY_HP) >= 800 and machine.b(MENU_MEMBER) < 3:
            break
        if run.dialog() or attempt < 2:
            machine.run(3, "A")
        machine.run(40)
    assert machine.w(spekkio.ENEMY_HP) >= 800 and machine.b(MENU_MEMBER) < 3, "Spekkio's first menu"
    run.say("Marle battle magic", machine.b(0x7E5EE6), "affinities", list(machine.peek(0x7E5EEC, 4)))
    records = []

    def observe(bot):
        step = bot.step
        shown = {}

        def watched(frames=1, buttons=0):
            before = bot.state
            state = step(frames, buttons)
            if state.frame < before.frame:
                records[:] = [r for r in records if r["frame"] < state.frame]
                shown.clear()
            numbers = machine.peek(0x7EAD9C, 44 * 4)
            for entry in range(44):
                number = numbers[4 * entry:4 * entry + 4]
                if number[2] == 3:
                    shown[entry % 11] = int.from_bytes(number[:2], "little")
            for target in range(4):
                lost = before.hp(target) - state.hp(target)
                if lost <= 0:
                    continue
                actor = machine.b(0x7EB18B)
                action = machine.b(0x7EAE93)
                # The effect's scratch element is cleared before HP changes.
                # Enemy tech records retain it (effect_data.h); Marle uses Ice.
                element = machine.b(0xCC6FCA + 11 * action + 2) if actor == 3 else 0x20
                number = shown.get(target, lost)
                record = dict(frame=machine.frame, actor=actor, target=target,
                              element=element, number=number, hp_lost=lost,
                              action=action)
                records.append(record)
                run.say("damage", record)
            return state
        bot.step = watched

    def policy(bot, state, rng, inventory):
        hit = any(r["actor"] == 3 and r["target"] == 1 and r["element"] & 0xE0 for r in records)
        if state.member == 1 and hit and (state.hp(1) >= 100 or state.hp(3) < 100):
            return "tech", 2, 3  # Ice, the third single-tech entry
        for target in range(3):
            if state.hp(target) == 0:
                return policies.use(inventory, policies.REVIVE, target)
        low = min(range(3), key=lambda slot: state.hp(slot))
        if state.hp(low) < 160:
            return policies.use(inventory, policies.MID_TONIC, low)
        # Keep the fight going until Marle has received a protected element.
        if state.member == 1 and hit:
            return "tech", 2, 3  # Ice, the third single-tech entry
        return "attack", 3

    run.fight(policy, lambda r, s: s.hp(3) == 0, party=3, seed=seed, setup=observe, max_frames=20000)
    assert any(r["actor"] == 1 and r["target"] == 3 for r in records), "Marle dealt damage"
    assert any(r["actor"] == 3 and r["target"] == 1 and r["element"] & 0xE0 for r in records), "Marle took elemental magic"
    assert run.wait_control(), "the field back after Spekkio"
    with open(run.prefix + "_damage.json", "w", encoding="utf-8") as output:
        json.dump(records, output, indent=2)
    return records


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    try:
        phases = ["loaded", "empty", "bridge", "north", "canyon", "lower", "upper", "gate", "spekkio", "won"]
        start = run.start_at(phases)
        if start <= 0:
            machine.run(1620)
            machine.run(5, "START")
            machine.run(2420 - machine.frame)
            assert run.select_slot(1)
            machine.run(4, "A")
            machine.run(240)
            assert run.map_id() == "f101"
            run.checkpoint("loaded")
        if start <= 1:
            unequip_marle(machine)
            run.checkpoint("empty")
        if start <= 2:
            assert run.world_go(248, 512), "Zenan Bridge's southern entrance"
            machine.run(3, "A")
            machine.run(120)
            assert run.wait_control() and run.map_id() == "8700"
            run.checkpoint("bridge")
        if start <= 3:
            machine.run(600, "RIGHT+B")
            machine.run(90)
            machine.run(20, "DOWN")
            machine.run(100, "RIGHT+B")
            machine.run(90)
            machine.run(35, "UP")
            machine.run(100, "RIGHT+B")
            machine.run(90)
            assert run.map_id() == "f101" and run.world_pos()[1] == 448, "across the bridge"
            run.checkpoint("north")
        if start <= 4:
            assert run.world_go(440, 208), "Truce Canyon's entrance"
            machine.run(3, "A")
            machine.run(120)
            assert run.wait_control() and run.map_id() == "7000"
            run.checkpoint("canyon")
        if start <= 5:
            machine.run(100, "UP+B")
            machine.run(180)
            machine.run(60, "LEFT+B")
            machine.run(150, "UP+B")
            machine.run(400)
            assert machine.b(MENU_MEMBER) < 3, "the lower canyon fight"
            run.fight("party", lambda r, s: not policies.enemies(s), party=3)
            assert run.wait_control(), "the lower canyon back in hand"
            run.checkpoint("lower")
        if start <= 6:
            assert run.go(200, 176), "below the upper ambush"
            machine.run(30, "UP")
            machine.run(400)
            assert machine.b(MENU_MEMBER) < 3, "the upper canyon fight"
            small_fight(run)
            run.checkpoint("upper")
        if start <= 7:
            assert run.exit_to("7100"), "the gate clearing"
            run.checkpoint("gate")
        if start <= 8:
            assert run.wait_control(), "the gate clearing in hand"
            machine.run(60, "RIGHT")
            machine.run(120)
            machine.run(20, "UP")
            machine.run(16, "RIGHT")
            machine.run(3, "A")
            machine.run(1200)
            assert run.map_id() == spekkio.END_OF_TIME and run.wait_control(), "through the time gate"
            # Across the pillar platform and its diagonal bridge. The door's
            # collision trigger is narrow: approach its centre before going up.
            machine.run(1)
            machine.run(70, "RIGHT+B")
            machine.run(70, "DOWN+B")
            machine.run(60)
            machine.run(70, "DOWN+RIGHT+B")
            machine.run(40, "RIGHT+B")
            machine.run(60)
            machine.run(40, "RIGHT+B")
            machine.run(25, "UP")
            machine.run(3, "A")
            machine.run(120)
            machine.run(7, "RIGHT")
            machine.run(60, "UP")
            machine.run(90)
            assert run.map_id() == spekkio.SPEKKIO_ROOM, "through Spekkio's door"
            run.checkpoint("spekkio")
        if start <= 9:
            assert run.wait_control(), "Spekkio's room in hand"
            assert machine.peek(MARLE_GEAR, 2) == b"\0\0", "Marle still has both slots empty"
            fight_spekkio(run, args.seed)
            run.checkpoint("won")
        run.finish("Marle's unequipped magic against Spekkio")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
