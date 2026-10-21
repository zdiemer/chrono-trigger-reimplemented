"""Beat Azala and the Black Tyrano in Tyrano Lair, and watch Lavos fall (scene-program mode 4).

Save: ctsave08 slot 1 (fantasyanime archive; Crono, Ayla and Marle at Tyrano Lair's save point, $12F).

The way: out of the save point's room into the lair's hall ($12A), through to the corridor before
the throne room ($12C), where walking past its twentieth tile row starts Azala's look down from
the stairs, and up into the throne room ($12D). Confirm at the throne's foot starts the scene
(Azala's challenge and the battle with Azala riding the Black Tyrano, formation $93); the battle
bot fights it. After it Azala's last words, and event $FF $24 hands the screen to the scene
program at mode 4 ($7E:53BB): Lavos falling on the 65,000,000 B.C. world, which no other movie
plays. The route stops once the scene has run its course and the field is back.
"""
from .. import policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Tyrano Lair, 65,000,000 B.C.: from the save point's room through the hall and the corridor (Azala's look down from the
# stairs) into the throne room; Azala's challenge and the battle with Azala and the Black Tyrano, fought by the battle bot;
# Azala's last words, then Lavos's fall, the scene program's mode 4 (event $FF $24), until the field is back (route
# black_tyrano).
# Save: ctsave08 slot 1 (oracle/saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/fantasyanime/ctsave08-zeal.srm"
SLOT = 0
PHASES = ["loaded", "hall", "corridor", "throne", "battle", "won", "scene", "after"]

SAVE_ROOM, HALL, CORRIDOR, THRONE_ROOM = "2f01", "2a01", "2c01", "2d01"
SCENE_MODE = 0x7E0384           # the scene program's mode, as $C3:0000 stores the event's operand
LAVOS_FALLS = 0x24              # event $FF $24: mode 4
CORRIDOR_SCENE_ROW = 20         # the corridor's scene starts past this tile row ($12C, object 0)
THRONE_FOOT = 16                # the first of the objects at the throne's foot ($12D's object $10)
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
        assert run.map_id() == SAVE_ROOM, "Tyrano Lair's save point"
        run.checkpoint("loaded")
    if start <= 1:
        assert run.exit_to(HALL), "the lair's hall"
        run.checkpoint("hall")
    if start <= 2:
        assert run.exit_to(CORRIDOR), "the corridor before the throne room"
        run.checkpoint("corridor")
    if start <= 3:
        row = CORRIDOR_SCENE_ROW * 16 - 4
        assert run.walk(lambda m, px, py: py < row) is not None, "up the corridor to Azala's look down"
        assert run.wait_control(), "Azala gone up the stairs"
        assert run.exit_to(THRONE_ROOM), "the throne room"
        run.checkpoint("throne")
    if start <= 4:
        # The scene starts from the throne's foot: objects $10-$14 stand along tile row 10 and, spoken
        # to, call object $15's function 3 (Azala's challenge, then the battle).
        tx, ty = run.object_pos(THRONE_FOOT)
        assert run.walk(lambda m, px, py: abs(px - tx) <= 3 and abs(py - (ty + 18)) <= 3) is not None, \
            "below the throne"
        machine.run(2, "UP")
        machine.run(8)
        machine.run(3, "A")
        assert run.dialogs_until(lambda: machine.b(BATTLE_MENU_MEMBER) < 0x80, 8000), "the battle's first menu"
        run.checkpoint("battle")
    if start <= 5:
        seen = []

        def won(r, state):
            living = policies.enemies(state)
            if living:
                seen.append(True)
            return bool(seen) and not living and any(state.hp(slot) for slot in range(3))
        run.fight("party_with_lapis", won, party=3, seed=args.seed)
        run.checkpoint("won")
    if start <= 6:
        assert run.dialogs_until(lambda: machine.b(SCENE_MODE) == LAVOS_FALLS, 12000), "Lavos's fall begins"
        run.say("scene mode", hex(machine.b(SCENE_MODE)), "frame", machine.frame)
        run.checkpoint("scene")
    if start <= 7:
        assert run.dialogs_until(lambda: run.map_id() != THRONE_ROOM or run.find_player() is not None, 20000), \
            "the field back after the scene"
        run.say("after the scene, map", run.map_id(), "frame", machine.frame)
        run.checkpoint("after")
    machine.screenshot(run.prefix + "_end.png")
    run.finish("Lavos's fall after the Black Tyrano")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
