"""Win Son of Sun's flame puzzle in the Sun Palace, 2300 A.D."""
from .. import addresses as A, policies
from ..campaign import Run, route_arguments, run_route
from .epoch_lavos import wait_until, DIAL_PERIOD

SAVE = "oracle/saves/fantasyanime/ctsave13-side_quests2.srm"
SLOT = 2
PHASES = ["loaded", "boarded", "future", "landed", "palace", "battle", "won", "after"]
HEADER = """\
# Sun Palace, 2300 A.D.: fly the Epoch from ctsave13 slot 3 and win Son of Sun's flame puzzle.
# The policy reads the flames' reaction state after each shuffle; stop with the field back in hand (route son_of_sun).
# Save: ctsave13 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def world_pos(m):
    return m.w(A.WORLD_X), m.w(A.WORLD_Y)


def world_map(m):
    return m.peek(A.MAP_ID, 2).hex()


def fly_to(run, x, y):
    """Fly east and south to a landing approach, allowing the last tile to finish."""
    m = run.machine
    for axis, target in enumerate((x - 8, y - 8)):
        for _ in range(1200):
            pos = run.world_pos()[axis]
            if abs(pos - target) <= 3:
                break
            direction = ("RIGHT" if pos < target else "LEFT") if axis == 0 else ("DOWN" if pos < target else "UP")
            m.run(1, direction)
        else:
            raise AssertionError("the Epoch reaches its approach")
        m.run(12)


def flame_policy(bot, state, rng, inventory):
    """Heal, then attack the flame whose reaction does not counter.

    Enemy $F7's AI (tools.disasm.battlescript) reacts to Roulette Shuffle
    by setting magic defence, stat offset $3C, to $62 on the chosen flame
    and $63 on the others. Its $63 reaction counters; the other flame's
    ordinary attack passes damage to the eye. Re-read it for each turn.
    The stat block begins three bytes before addresses.BATTLE_STATS (HP).
    """
    action = policies.survival(state, rng, inventory, state.member, range(3),
                               healer_threshold=0.8, self_threshold=0.7)
    if action:
        return action
    real = [slot for slot in range(4, 9)
            if bot.machine.b(A.BATTLE_STATS - 3 + slot * 0x80 + 0x3C) == 0x62]
    return ("attack", real[0] if real else 4)


def play(run, seed=1):
    """Play through the fight; also the approach for the charging hand-run."""
    m = run.machine
    start = run.start_at(PHASES)
    if start <= 0:
        m.run(1620)
        m.run(5, "START")
        m.run(2420 - m.frame)
        run.select_slot(SLOT)
        m.run(4, "A")
        assert wait_until(m, lambda: run.map_id() not in ("0000", "b101"), 2000)
        m.run(620)
        assert run.map_id() == "f001", "the 1000 A.D. world map"
        assert m.b(0x7F013A) == 0, "Son of Sun unbeaten and the stone not yet taken"
        run.say("loaded", run.map_id(), run.world_pos(), "flags", m.peek(0x7F003A, 1).hex(), m.peek(0x7F013A, 1).hex())
        run.checkpoint("loaded")
    if start <= 1:
        # The Epoch is on the save's starting tile; ow_bfs only confirms
        # after a move, so it cannot find this boarding from its root.
        m.run(4, "A")
        assert wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 2, 1000)
        m.run(200)
        run.checkpoint("boarded")
    if start <= 2:
        m.run(4, "Y")
        m.run(100)
        for _ in range(8):
            run.say("dial", m.b(DIAL_PERIOD))
            if m.b(DIAL_PERIOD) == 1:
                break
            m.run(4, "UP")
            m.run(56)
        assert m.b(DIAL_PERIOD) == 1, "2300 A.D. on the time dial"
        m.run(4, "A")
        assert wait_until(m, lambda: run.map_id() == "f201", 4000)
        m.run(500)
        run.say("future", run.world_pos(), "travel", m.b(A.OVERWORLD_TRAVEL))
        run.checkpoint("future")
    if start <= 3:
        fly_to(run, 544, 936)
        m.run(4, "A")
        assert wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 0, 1000), "land east of the Sun Palace"
        m.run(120)
        run.checkpoint("landed")
    if start <= 4:
        # The island's walk, found on machine copies. Checking each landing
        # of a step avoids replaying the Epoch's suspended native routines
        # hundreds of times in an overworld breadth-first search.
        for direction, destination in [("UP", (544, 920)), ("UP", (544, 904)), ("UP", (544, 896)),
                                       ("LEFT", (536, 896)), ("DOWN", (536, 912)), ("LEFT", (520, 912)),
                                       ("UP", (520, 904)), ("LEFT", (504, 904)), ("LEFT", (488, 904)),
                                       ("UP", (488, 888))]:
            m.run(16, direction)
            m.run(4)
            assert run.world_pos() == destination, "the island's path to the palace"
        m.run(3, "A")
        m.run(90)
        assert run.wait_control() and run.map_id() == "fb00", "the palace field"
        run.checkpoint("palace")
    if start <= 5:
        assert run.find_player(), "the party in the palace"
        for _ in range(20):
            if m.b(0x7F013A) & 1:
                break
            m.run(30, "UP")
        assert m.b(0x7F013A) & 1, "Son of Sun awakens"
        assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 3
                                 and m.w(A.BATTLE_STATS + 3 * 0x80 + 2) == 12000
                                 and 0 < m.w(A.BATTLE_STATS + 2) < 1000, 5000), "the battle is ready"
        run.checkpoint("battle")
    if start <= 6:
        # The eye begins at 12000 HP and its AI ends the fight at <=10000;
        # the flames keep their HP, so the generic all-enemies-dead test
        # does not describe this scripted victory.
        run.fight(flame_policy, lambda r, s: s.hp(3) <= 10000, party=3,
                  seed=seed, max_frames=30000, max_tries=60)
        run.checkpoint("won")
    if start <= 7:
        assert run.wait_control(12000), "the field back in hand after Son of Sun"
        assert run.map_id() == "fb00" and m.b(0x7F013A) & 1
        run.checkpoint("after")


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    play(run, args.seed)
    m = run.machine
    m.screenshot(run.prefix + "_end.png")
    run.finish("Son of Sun beaten and the field back in hand")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
