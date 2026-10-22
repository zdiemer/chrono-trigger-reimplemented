"""A second tech sweep: Spekkio fought at the End of Time by two trios in turn, for the single,
double and triple techs the first sweep's trio doesn't have.

Save: zophar chrono slot 1 (all seven at the End of Time, $1D0, levels 58-63, every tech learned and
the bag full: the Black Omen destroyed, every side quest done). The End of Time is the one place the
whole party can be exchanged (Y on the field opens the gate's screen there), and Spekkio's room
($1D1) is a door away: at these levels he has 10000 HP, attacks took nothing off it here, and he gives the
fight up once techs have worn him down far enough, which is a dozen to thirty techs; and he can be
fought as often as he is spoken to. Each trio fights him with the tech-sweep policy (double and
triple techs first, a tech it couldn't take skipped for the turn, the strongest cast so far once the
lists are swept), and again if a fight left more than a few of its techs uncast and cast enough to
be worth another; then the party walks back out and the exchange brings in the next trio
(docs/movies-wanted.md, "Every tech").
"""
import json
import os

from .. import policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# The second tech sweep: load the save (all seven at the End of Time), then for Lucca, Robo and Ayla and then Crono,
# Marle and Frog, change the party at the End of Time's exchange (Y), walk into Spekkio's room and fight him, twice, with
# the tech-sweep policy casting each tech the trio's lists show once (double and triple techs first, then the singles),
# healing and reviving with items (route tech_sweep_spekkio).
# Save: zophar chrono slot 1 (oracle/saves/zophar, zophar.net Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/zophar/chrono.srm"
SLOT = 0
END_OF_TIME, SPEKKIO_ROOM = "d001", "d101"
SPEKKIO = 8                     # his object in $1D1's script
SPEKKIO_SPOT = (120, 142)       # above him, facing down: the side he is spoken to from
DOORWAY = (96, 224)             # where the party comes into his room
DOORWAY_SLOT = 4080             # the machine copy leave_spekkio tries the way out from

CRONO, MARLE, LUCCA, ROBO, FROG, AYLA, MAGUS = range(7)
# Each trio brings its pairs' double techs and any triple tech they make: Lucca, Robo and Ayla's
# singles and theirs, then Crono, Marle and Frog's -- Crono and Marle's own double techs among them,
# which the first sweep's policy never reached. Two trios: a third (Crono, Lucca and Frog was next)
# would be some 70000 frames more for about 0.4 KiB, the second's share (docs/movies-wanted.md).
TRIOS = [
    (LUCCA, ROBO, AYLA),
    (CRONO, MARLE, FROG),
]
FIGHTS_A_TRIO = 2
FEW_NEW = 3                     # a fight that cast fewer new techs than this isn't fought again,
FEW_LEFT = 4                    # nor one that left fewer than this uncast (a fight is some 30000 frames)

PARTY = 0x7E2980                # the party's record: the active three, then the reserve's six places
EMPTY = 0x80
MENU_SCREEN = 0x7E0067          # menu_screen: 7 the gate's exchange
CURSOR = 0x7E0054               # menu_cursor_index: 0-2 the active rows, 3-6 the reserve's shown
EXCHANGE = 7
ENEMY_HP = 0x7E5E30 + 3 * 0x80  # Spekkio, the first enemy slot


def press(machine, button, gap=30, hold=3):
    machine.run(hold, button)
    machine.run(gap)


def party_record(machine):
    return list(machine.peek(PARTY, 9))


def reserve(record):
    """The reserve as the exchange lists it: the places that hold a member, in order."""
    return [member for member in record[3:] if not member & EMPTY]


def move_to(machine, want):
    for _ in range(12):
        here = machine.b(CURSOR)
        if here == want:
            return True
        if (here < 3) != (want < 3):
            press(machine, "RIGHT" if want >= 3 else "LEFT")
        else:
            press(machine, "DOWN" if want > here else "UP")
    return machine.b(CURSOR) == want


def exchange(run, trio):
    """Open the End of Time's exchange and bring `trio` into the active party, one swap a row."""
    machine = run.machine
    record = party_record(machine)
    if sorted(record[:3]) == sorted(trio):
        return
    press(machine, "Y", 80)
    assert machine.b(MENU_SCREEN) == EXCHANGE, "the exchange opens"
    for row in range(3):
        record = party_record(machine)
        if record[row] in trio:
            continue
        coming = [member for member in reserve(record) if member in trio and member not in record[:3]]
        member = coming[0]
        assert move_to(machine, row), "the cursor on active row %d" % row
        press(machine, "A")
        assert move_to(machine, 3 + reserve(record).index(member)), "the cursor on the reserve's member"
        press(machine, "A", 60)
        assert party_record(machine)[row] == member, "member %d into row %d" % (member, row)
    for _ in range(4):
        if machine.b(MENU_SCREEN) != EXCHANGE:
            break
        press(machine, "B", 60)
    assert run.wait_control(), "the field back after the exchange"
    run.say("party", party_record(machine)[:3], "frame", machine.frame)


def enter_spekkio(run):
    """Into Spekkio's room. Not exit_to: its search stops at the first window that opens, and the
    pillar of light asks where to go when the party walks into it, which a search with other timing
    can come to before the door (and pressing on through it takes the party through a gate)."""
    machine = run.machine
    assert run.wait_control(), "the party in hand"
    moves = run.walk(lambda m, px, py: m.peek(0x7E0100, 2).hex() == SPEKKIO_ROOM)
    machine.run(60)
    run.say("into Spekkio's room, steps", moves and len(moves), "frame", machine.frame)
    assert run.map_id() == SPEKKIO_ROOM, "Spekkio's room"


def edge_to(run, x, y, tolerance=1):
    """Walk to (x, y) a frame at a time, across and then up or down: where a fight left the party
    (another timing's fight ends with it elsewhere in the room), the search in cells can miss a spot as
    narrow as the doorway's, which the walk along the room's open floor reaches."""
    machine = run.machine
    for _ in range(300):
        px, py = run.me()
        if abs(px - x) > tolerance:
            machine.run(1, "LEFT" if px > x else "RIGHT")
        elif abs(py - y) > tolerance:
            machine.run(1, "UP" if py > y else "DOWN")
        else:
            run.say("edged to", (x, y), "me", run.me(), "frame", machine.frame)
            return True
    return False


def leave_spekkio(run):
    """Out of Spekkio's room: its doorway is a few pixels wide at the room's left corner, which a search
    in whole cells steps past, and whether a walk down from beside it goes through depends on the
    pixel and the walk's step. So from where the party came in it edges left a frame at a time and
    walks down, putting the machine back and edging one frame further while the walk misses."""
    machine = run.machine
    assert run.wait_control(), "the party in hand"
    assert run.go(*DOORWAY, tolerance=2) or edge_to(run, *DOORWAY), "inside the doorway"
    machine.mark(DOORWAY_SLOT)
    for edge in range(1, 16):
        machine.restore(DOORWAY_SLOT)
        machine.run(edge, "LEFT")
        for _ in range(12):
            machine.run(10, "DOWN")
            if run.map_id() == END_OF_TIME:
                break
        if run.map_id() == END_OF_TIME:
            break
    machine.free(DOORWAY_SLOT)
    assert run.map_id() == END_OF_TIME, "back to the End of Time"
    assert run.wait_control(), "the End of Time in hand"


def fight_spekkio(run, policy, setup, seed):
    machine = run.machine
    assert run.go(*SPEKKIO_SPOT, tolerance=3), "beside Spekkio"
    machine.run(2, "DOWN")
    machine.run(8)
    started = False
    for _ in range(40):
        if machine.w(ENEMY_HP) == 10000 and machine.b(0x7E95D5) < 0x80:
            started = True
            break
        if run.dialog() or _ < 2:
            machine.run(3, "A")
        machine.run(40)
    assert started, "Spekkio's fight starts"
    run.fight(policy, lambda r, s: s.max_hp(3) == 10000 and s.hp(3) == 0, party=3, seed=seed, setup=setup)
    assert run.wait_control(), "the field back after the fight"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    phases = ["loaded"] + ["trio%d" % n for n in range(len(TRIOS))]
    start = run.start_at(phases)
    record_path = args.prefix + "_cast.json"
    cast_ids, refused, damage = set(), set(), {}
    if start > 0 and os.path.exists(record_path):
        cast_ids.update(json.load(open(record_path))["%d" % (start - 1)])
    sweep = policies.tech_sweep(cast_ids, refused=refused, finish_with_techs=True, combinations_first=True,
                                damage=damage)
    shown = set()

    def policy(bot, state, rng, inventory):
        shown.update(tech_id for _, tech_id in policies.tech_list_entries(bot, state.member))
        return sweep(bot, state, rng, inventory)
    setup = lambda bot: policies.tracking_do(bot, cast_ids, refused, damage)

    def note(phase):
        saved = json.load(open(record_path)) if os.path.exists(record_path) else {}
        saved["%d" % phase] = sorted(cast_ids)
        json.dump(saved, open(record_path, "w"))

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
        assert run.wait_control(), "the End of Time in hand"
        assert run.map_id() == END_OF_TIME, "the End of Time"
        note(0)
        run.checkpoint("loaded")
    for n, trio in enumerate(TRIOS):
        if start > n + 1:
            continue
        if run.map_id() == SPEKKIO_ROOM:
            leave_spekkio(run)
        exchange(run, trio)
        enter_spekkio(run)
        shown.clear()
        for fight in range(FIGHTS_A_TRIO):
            before = len(cast_ids)
            fight_spekkio(run, policy, setup, args.seed + 100 * n + fight)
            run.say("trio", trio, "fight", fight, "techs cast", len(cast_ids), sorted(cast_ids),
                    "not yet", sorted(shown - cast_ids), "frame", machine.frame)
            if len(shown - cast_ids) < FEW_LEFT or len(cast_ids) - before < FEW_NEW:
                break
        note(n + 1)
        run.checkpoint("trio%d" % n)
    run.say("end frame", machine.frame, "techs cast", len(cast_ids), sorted(cast_ids))
    run.finish("the second tech sweep")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
