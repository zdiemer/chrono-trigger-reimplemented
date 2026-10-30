"""Hand-run the Moon Stone's placement in the prehistoric Sun Keep after Son of Sun."""
from .. import addresses as A, bfs
from ..campaign import Run, route_arguments, run_route
from . import son_of_sun as battle
from .epoch_lavos import DIAL_PERIOD, wait_until

SAVE = battle.SAVE
PHASES = battle.PHASES + ["stone", "aboard", "prehistory", "keep", "planted"]
QUEST = 0x7F013A
HEADER = """\
# Hand-run Sun Stone route: Son of Sun, the Moon Stone recovered, then placed in the Sun Keep in 65,000,000 B.C.
# Kept outside the movie library: repeats battle-son-of-sun's fight (route sun_stone).
# Save: ctsave13 slot 3 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def travel(run, period, destination):
    m = run.machine
    m.run(4, "Y")
    m.run(100)
    for _ in range(8):
        now = m.b(DIAL_PERIOD)
        if now == period:
            break
        m.run(4, "DOWN" if now < period else "UP")
        m.run(56)
    assert m.b(DIAL_PERIOD) == period
    m.run(4, "A")
    assert wait_until(m, lambda: run.map_id() == destination, 4000)
    m.run(500)


def land_near(run):
    """Try nearby ground on copies, keeping only the successful landing."""
    m = run.machine
    m.mark(4080)
    for dy in (0, 16, -16, 32, -32, 48, -48, 64, -64):
        for dx in (0, 16, -16, 32, -32, 48, -48, 64, -64):
            m.restore(4080)
            for distance, positive, negative in ((dx, "RIGHT", "LEFT"), (dy, "DOWN", "UP")):
                if distance:
                    m.run(abs(distance) // 2, positive if distance > 0 else negative)
                    m.run(16)
            m.run(4, "A")
            m.run(240)
            if m.b(A.OVERWORLD_TRAVEL) == 0:
                m.free(4080)
                return
    m.restore(4080)
    m.free(4080)
    raise AssertionError("ground near the Sun Keep to land on")


def play(run, seed=1):
    m = run.machine
    start = run.start_at(PHASES)
    if start < len(battle.PHASES):
        battle.play(run, seed)
    if start <= 8:
        assert run.find_player()
        assert run.go(256, 184, tolerance=5), "climb to the Moon Stone"
        assert run.talk(17, done=lambda: bool(m.b(QUEST) & 2)), "recover the Moon Stone"
        assert run.wait_control()
        run.checkpoint("stone")
    if start <= 9:
        assert run.exit_to("f201"), "leave the palace"
        m.run(240)
        moves, _ = bfs.ow_bfs(m, lambda m, x, y: m.b(A.OVERWORLD_TRAVEL) != 0,
                              battle.world_pos, battle.world_map, confirm=True, max_nodes=250)
        assert moves is not None and wait_until(m, lambda: m.b(A.OVERWORLD_TRAVEL) == 2, 1000)
        m.run(200)
        run.checkpoint("aboard")
    if start <= 10:
        travel(run, 6, "f301")
        run.checkpoint("prehistory")
    if start <= 11:
        exits = [m.peek(0x7E7E00 + 7 * i, 7) for i in range(128)]
        keep = next(e for e in exits if e[0] & 0x80 and (int.from_bytes(e[3:5], "little") & 0x1FF) == 0x147)
        battle.fly_to(run, (keep[0] & 0x7F) * 16, (keep[1] & 0x3F) * 16)
        land_near(run)
        assert run.world_exit(max_nodes=250) and run.map_id() == "4701"
        assert run.wait_control()
        run.checkpoint("keep")
    if start <= 12:
        assert run.find_player()
        assert run.talk(8, done=lambda: bool(m.b(QUEST) & 4)), "place the Moon Stone"
        assert run.wait_control() and m.b(QUEST) == 7
        run.checkpoint("planted")
    assert run.map_id() == "4701" and m.b(QUEST) == 7
    assert not any(item == 0xDE and count for item, count in
                   zip(m.peek(A.INVENTORY_ITEMS, A.INVENTORY_SIZE), m.peek(A.INVENTORY_COUNTS, A.INVENTORY_SIZE)))


def main():
    args = route_arguments(__doc__, save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    play(run, args.seed)
    m = run.machine
    m.screenshot(run.prefix + "_end.png")
    run.finish("the Moon Stone left to charge in prehistory")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
