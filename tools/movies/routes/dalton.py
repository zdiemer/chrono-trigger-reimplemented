"""Defeat Dalton Plus, destroy the Blackbird, and bring the winged Epoch back to the field."""
from ..campaign import Run, route_arguments, run_route
from .. import policies
from .blackbird_save import travel

SAVE = "saves/recorded/blackbird-before-dalton.srm"
PHASES = ["loaded", "ducts", "corridor", "wing", "golem", "dalton", "won", "wings", "field"]
HEADER = """\
# Dalton Plus, the Blackbird's destruction and the Epoch's wings (route dalton).
# Save: blackbird-before-dalton slot 1 (saves/recorded), made by normal play from ctsave11 slot 1.
# Starts after events-blackbird's opening, with the party's equipment and inventory recovered.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


# The Blackbird's windows are hurried along every eighth frame.
DIALOG_PACE = 8


def boss_policy(bot, state, rng, inventory):
    if any(state.hp(i) < state.max_hp(i) * .35 for i in range(3)):
        return policies.party(bot, state, rng, inventory)
    member = state.member
    character = policies.party_ids(bot)[member]
    choices = {1: [(5, 8)], 2: [(7, 20), (6, 15), (5, 8)], 3: [(3, 4), (0, 1)]}
    for tech, cost in choices.get(character, []):
        if not policies.single_tech_known(bot, member, tech):
            continue
        if state.mp(member) < cost:
            for item in (policies.FULL_ETHER, policies.HYPER_ETHER, policies.MID_ETHER, policies.ETHER):
                action = policies.use(inventory, item, member)
                if action:
                    return action
        else:
            return ("tech", tech, None)
    return policies.party(bot, state, rng, inventory)


def fight(run, boss=False):
    seen = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(i) for i in range(3))

    assert run.dialogs_until(lambda: run.machine.b(0x7E95D5) < 0x80, 6000), "battle menu"
    run.machine.run(180)
    run.fight(boss_policy if boss else "party", won, max_frames=24000, delays=(0,))


def play(run, start=1, seed=1):
    """From the cell's save point, the equipment recovered, to the winged
    Epoch landed on the 12000 B.C. map and in hand; `start` is the index in
    PHASES of the first phase to run."""
    m = run.machine
    pace, run.dialog_pace = run.dialog_pace, DIALOG_PACE
    try:
        if start <= 1:
            assert run.talk(11, done=lambda: run.map_id() == "7801")
            assert run.wait_control()
            run.checkpoint("ducts")
        if start <= 2:
            # Follow the long connected duct around its bends. Searching the
            # whole duct map exhausts the field walker's checkpoint budget.
            for frames, direction in ((16, "DOWN"), (88, "RIGHT"), (224, "UP"),
                                      (440, "LEFT"), (360, "UP"), (168, "LEFT"), (80, "UP")):
                m.run(frames, direction)
                m.run(8)
            assert run.map_id() == "7101"
            assert run.wait_control()
            run.checkpoint("corridor")
        if start <= 3:
            assert run.wait_control()
            if not m.b(0x7F00D1) & 8:
                # The switch is reached from its right, beside the closed
                # bulkhead; the usual spot below an object is inside it.
                for frames, direction in ((100, "DOWN"), (130, "LEFT"), (114, "DOWN"),
                                          (32, "LEFT"), (3, "A")):
                    m.run(frames, direction)
                    m.run(30)
                m.run(80)
                assert m.b(0x7F00D1) & 8, "open the west bulkhead"
            for location in ("7601", "de01", "6b01"):
                assert travel(run, location)
            assert run.wait_control()
            run.checkpoint("wing")
        if start <= 4:
            # Run up against the wing's downward drift, then west around the
            # engine housings. Walking alone cannot get above them.
            for frames, direction in ((160, "B+UP"), (480, "B+LEFT"), (180, "B+DOWN"),
                                      (160, "B+UP"), (100, "B+LEFT"), (160, "B+DOWN")):
                m.run(frames, direction)
            assert not m.b(0x7E011F), "Golem Boss's introduction"
            fight(run, True)
            assert run.dialogs_until(lambda: m.b(0x7F00D2) >= 1, 6000)
            assert m.b(0x7F00D2) >= 1, "Golem Boss beaten"
            run.checkpoint("golem")
        if start <= 5:
            assert run.dialogs_until(lambda: run.map_id() == "7901" and m.b(0x7F00D2) == 7
                                     and m.b(0x7E95D5) < 0x80, 20000), "Dalton Plus"
            run.checkpoint("dalton")
        if start <= 6:
            fight(run, True)
            began = m.frame
            while m.b(0x7F00D2) < 8 and m.frame - began < 6000:
                m.run(3, "A" if run.dialog() else "Y")
                m.run(8)
            assert m.b(0x7F00D2) >= 8, "test the Epoch's controls"
            run.checkpoint("won")
        if start <= 7:
            assert run.dialogs_until(lambda: run.map_id() == "7901" and m.b(0x7F00D2) == 9, 6000)
            began = m.frame
            while m.b(0x7F00D2) == 9 and m.frame - began < 6000:
                if run.dialog():
                    m.run(3, "A")
                else:
                    m.run(3, "X")
                m.run(8)
            assert m.b(0x7F00D2) == 10, "fire the Epoch"
            assert run.dialogs_until(lambda: run.map_id() == "f601", 16000), "back on the world map"
            m.run(180)
            assert m.b(0x7F00BA) & 0x80
            run.checkpoint("wings")
        if start <= 8:
            m.run(3, "A")
            m.run(240)
            assert run.dialogs_until(lambda: run.map_id() == "f601" and m.b(0x7E027E) == 0,
                                     6000), "finish the landing dialogue"
            m.run(90)
            before = run.world_pos()
            for direction in ("DOWN", "LEFT", "RIGHT", "UP"):
                m.run(16, direction)
                m.run(30)
                if run.world_pos() != before:
                    break
            assert run.map_id() == "f601" and run.world_pos() != before, "world-map control"
            run.checkpoint("field")
    finally:
        run.dialog_pace = pace


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              until=args.until)
    run.dialog_pace = DIALOG_PACE
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(0)
            m.run(4, "A")
            assert run.dialogs_until(lambda: run.map_id() == "7301", 3000)
            m.run(120)
            assert run.wait_control()
            assert m.b(0x7F0000) == 0xCF and m.b(0x7F00D2) == 0
            assert m.b(0x7F00AF) & 7 == 7 and m.b(0x7F00BA) & 2
            assert not m.b(0x7F00BA) & 0x80
            run.checkpoint("loaded")
        play(run, start, args.seed)
        run.finish("Dalton Plus beaten, Blackbird destroyed, the winged Epoch landed")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    except (AssertionError, RuntimeError):
        m.save_state(args.prefix + "_failed.st")
        m.screenshot(args.prefix + "_failed.png")
        run.say("failed at", m.frame, "map", run.map_id())
        raise
    finally:
        run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
