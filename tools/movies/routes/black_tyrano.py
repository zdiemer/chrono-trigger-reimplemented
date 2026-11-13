"""Beat Azala and the Black Tyrano in Tyrano Lair, and watch Lavos fall (scene-program mode 4).

Save: ctsave08 slot 1 (fantasyanime archive; Crono, Ayla and Marle at Tyrano Lair's save point, $12F).

The way: out of the save point's room into the lair's hall ($12A), through to the corridor before
the throne room ($12C), where walking past its twentieth tile row starts Azala's look down from
the stairs, and up into the throne room ($12D). Confirm at the throne's foot starts the scene
(Azala's challenge and the battle with Azala riding the Black Tyrano, formation $93); the battle
bot fights it. The route plays the movie's own way there (battle-black-tyrano) on time and starts
from the battle; the playthrough walks it. After it Azala's last words, and event $FF $24 hands the screen to the scene
program at mode 4 ($7E:53BB): Lavos falling on the 65,000,000 B.C. world, which no other movie
plays. The route stops once the scene has run its course and the field is back.
"""
from pathlib import Path

from .. import addresses as A
from .. import policies
from ..campaign import REPO, Run, route_arguments, run_route

HEADER = """\
# Tyrano Lair, 65,000,000 B.C.: from the save point's room through the hall and the corridor (Azala's look down from the
# stairs) into the throne room; Azala's challenge and the battle with Azala and the Black Tyrano, fought by the battle bot;
# Azala's last words, then Lavos's fall, the scene program's mode 4 (event $FF $24), until the field is back (route
# black_tyrano).
# Save: ctsave08 slot 1 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "saves/fantasyanime/ctsave08-zeal.srm"
MOVIE = Path(REPO) / "movies/saves/battle-black-tyrano.ctm"
# The movie's walk ends as the battle begins (its first command menu opens at frame 7233 in the reference).
WALK_ENDS = 7190
# The walk's phases (to tyrano_battle) are the playthrough's, which walks the lair
# (playthrough_leg-c); main starts at tyrano_battle from the movie's walk.
PHASES = ["loaded", "lair_hall", "lair_corridor", "lair_throne", "tyrano_battle", "tyrano_won", "lavos_falls",
          "tyrano_after"]

SAVE_ROOM, HALL, CORRIDOR, THRONE_ROOM = "2f01", "2a01", "2c01", "2d01"
SCENE_MODE = 0x7E0384           # the scene program's mode, as $C3:0000 stores the event's operand
LAVOS_FALLS = 0x24              # event $FF $24: mode 4
CORRIDOR_SCENE_ROW = 20         # the corridor's scene starts past this tile row ($12C, object 0)
THRONE_FOOT = 16                # the first of the objects at the throne's foot ($12D's object $10)


def play(run, start=1, seed=1):
    """From the save point's room to Lavos's fall watched and the field back;
    `start` is the index in PHASES of the first phase to run."""
    machine = run.machine
    if start <= 1:
        assert run.exit_to(HALL), "the lair's hall"
        run.checkpoint("lair_hall")
    if start <= 2:
        assert run.exit_to(CORRIDOR), "the corridor before the throne room"
        run.checkpoint("lair_corridor")
    if start <= 3:
        # Past the row the scene starts, which stops the leader within a pixel or two of it, wherever the
        # frame's phase puts the step.
        row = CORRIDOR_SCENE_ROW * 16
        assert run.walk(lambda m, px, py: py < row) is not None, "up the corridor to Azala's look down"
        assert run.wait_control(), "Azala gone up the stairs"
        assert run.exit_to(THRONE_ROOM), "the throne room"
        run.checkpoint("lair_throne")
    if start <= 4:
        # The scene starts from the throne's foot: objects $10-$14 stand along tile row 10 and, spoken
        # to, call object $15's function 3 (Azala's challenge, then the battle).
        tx, ty = run.object_pos(THRONE_FOOT)
        assert run.walk(lambda m, px, py: abs(px - tx) <= 3 and abs(py - (ty + 18)) <= 3) is not None, \
            "below the throne"
        machine.run(2, "UP")
        machine.run(8)
        machine.run(3, "A")
        assert run.dialogs_until(lambda: A.battle_menu_open(machine), 8000), "the battle's first menu"
        run.checkpoint("tyrano_battle")
    if start <= 5:
        seen = []

        def won(r, state):
            living = policies.enemies(state)
            if living:
                seen.append(True)
            return bool(seen) and not living and any(state.hp(slot) for slot in range(3))
        run.fight("party_with_lapis", won, party=3, seed=seed)
        run.checkpoint("tyrano_won")
    if start <= 6:
        assert run.dialogs_until(lambda: machine.b(SCENE_MODE) == LAVOS_FALLS, 12000), "Lavos's fall begins"
        run.say("scene mode", hex(machine.b(SCENE_MODE)), "frame", machine.frame)
        run.checkpoint("lavos_falls")
    if start <= 7:
        assert run.dialogs_until(lambda: run.map_id() != THRONE_ROOM or run.find_player() is not None, 20000), \
            "the field back after the scene"
        run.say("after the scene, map", run.map_id(), "frame", machine.frame)
        run.checkpoint("tyrano_after")


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 4:
        # The way to the throne is the movie's own, which the field plays on time.
        machine.play_movie(str(MOVIE), WALK_ENDS)
        assert run.dialogs_until(lambda: A.battle_menu_open(machine), 3000), "the battle's first menu"
        run.checkpoint("tyrano_battle")
    play(run, max(start, 5), args.seed)
    machine.screenshot(run.prefix + "_end.png")
    run.finish("Lavos's fall after the Black Tyrano")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
