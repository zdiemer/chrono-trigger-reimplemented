"""Beat the Guardian and its Bits in Arris Dome, 2300 A.D.

Save: ctsave02 slot 3 (fantasyanime archive; Crono, Lucca and Marle inside Arris Dome, $0D6).

The way: from the dome's hall down to its inner rooms ($0D8, $0DD) and into the Guardian's room
($0DB). Its object 9 stands at tile $08,$13 before the sealed door; confirm beside it (with story
bit $7F:00EC bit 0 clear, as on this save) wakes the Guardian: its Bits drop in, the party's
lines, and the battle (formation $93) the battle bot fights. The Guardian's AI brings beaten Bits
back (its action $10, $C1:9E78), which no other movie's enemy uses. The route stops once the
battle is won and the field is back in hand.
"""
from .. import policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Arris Dome, 2300 A.D.: from the hall through the inner rooms into the Guardian's room; confirm before the sealed door
# wakes the Guardian and its Bits, and the battle bot fights it to a win; then the field back in hand (route guardian).
# Save: ctsave02 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave02-future.srm"
SLOT = 2
PHASES = ["loaded", "inner", "lower", "room", "battle", "won", "after"]

HALL, INNER, LOWER, GUARDIAN_ROOM = "d600", "d800", "dd00", "db00"
WAKER = 9                       # $0DB's object 9, before the sealed door
BATTLE_MENU_MEMBER = 0x7E95D5   # whose battle menu is open, $80 and up for none


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
        assert run.wait_control(), "the party in hand"
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        assert run.map_id() == HALL, "Arris Dome"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.exit_to(INNER), "the dome's inner room"
        run.checkpoint("inner")
    if start <= 2:
        assert run.exit_to(LOWER), "the room below"
        run.checkpoint("lower")
    if start <= 3:
        assert run.exit_to(GUARDIAN_ROOM), "the Guardian's room"
        run.checkpoint("room")
    if start <= 4:
        tx, ty = run.object_pos(WAKER)
        for dx, dy, facing in run.SPOTS:
            if run.walk(lambda m, px, py: abs(px - tx - dx) <= 3 and abs(py - ty - dy) <= 3) is None:
                continue
            machine.run(2, facing)
            machine.run(8)
            machine.run(3, "A")
            machine.run(30)
            if not run.find_player():
                break
        assert run.dialogs_until(lambda: machine.b(BATTLE_MENU_MEMBER) < 0x80, 8000), "the battle's first menu"
        run.checkpoint("battle")
    if start <= 5:
        seen = []

        def won(r, state):
            living = policies.enemies(state)
            if living:
                seen.append(True)
            return bool(seen) and not living and any(state.hp(slot) for slot in range(3))
        run.fight("party", won, party=3, seed=args.seed)
        run.checkpoint("won")
    if start <= 6:
        assert run.wait_control(12000), "the field back in hand"
        run.say("after the battle, map", run.map_id(), "frame", machine.frame)
        run.checkpoint("after")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("the Guardian beaten")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
