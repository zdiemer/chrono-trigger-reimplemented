"""Dream Project ending, from the Millennial Fair telepod in New Game+.

Save: technoplaza ct-state16 slot 2 (Crono and Marle at the fair).

Leene Square to the telepods, the Gate, Lavos's shell, the inner cavern, the
inner Lavos and the Lavos Core, then the development room at the End of Time:
seven developers behind the light pillars (and the reaper statue off the
platform's edge) open the Gates to the Dream Team, whose five members end the
game.
"""
from .. import addresses as A
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Dream Project ending, from the telepod at the Millennial Fair in New Game+: load the fair save (Crono and Marle), walk
# Leene Square to the telepods, Taban's Super Dimension Warp speech, the right telepod's Gate, then Lavos's outer shell,
# the inner cavern walk (with dialogue), the inner Lavos with its arms and the humanoid form, and the Lavos Core fight (body,
# left bit and core) won with Luminaire, attacks, Ice 2, Haste, Cure 2, Life, Revive, Full Tonics, Elixirs, HyperEthers
# and MegaElixirs. Ending: the development room at the End of Time (Keizo Kokubo), the seven developer rooms behind the
# light pillars and the reaper statue, the Gates to the Dream Team, the five Dream Team members (the reversed-text screen
# gag), the fast staff credits and The End screen.
# Save: technoplaza ct-state16 slot 2 (games.technoplaza.net/snes/chronotrigger/savestates.php, Chrono Trigger (U).srm).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/technoplaza/ct-state16.srm"
PHASES = ["fair", "exhibit", "shell", "cavern", "inner", "core", "ending", "devroom", "tour", "kokubo", "credits"]

# The development room: which object in which map sets which bit of $7F:0078.
# The guard in 5501 opens the door to his developer instead of setting a bit.
DEV_ROOMS = [("5601", [(8, 0x20)]), ("5501", [(8, None), (12, 0x10)]), ("7a01", [(12, 0x08)]),
             ("8701", [(8, 0x01)]), ("5401", [(11, 0x40)]), ("5201", [(10, 0x02)])]
DEV_ROOM_MAPS = {room for room, _ in DEV_ROOMS} | {"8601", "5301"}
DREAM_TEAM = [(9, 0x02), (8, 0x04), (10, 0x01), (11, 0x08), (12, 0x10)]      # bits of $7F:0077


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              recomp=args.recomp, sound=args.sound_driver, until=args.until)
    machine = run.machine
    developers = lambda: machine.b(A.DEVELOPERS_TALKED)
    dream_team = lambda: machine.b(A.DREAM_TEAM_TALKED)
    start = run.start_at(PHASES)

    if start <= 0:
        # Title, then load the save the cursor already sits on (the last one saved).
        machine.run(1620)
        machine.run(5, "START")
        machine.run(2420 - machine.frame)
        machine.run(4, "A")
        for _ in range(200):
            machine.run(10)
            if run.map_id() not in ("0000", "b101"):
                break
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.world_go(424, 224)
        for _ in range(20):
            machine.run(4, "A")
            machine.run(30)
            if run.map_id() == "0500":
                break
        machine.run(120)
        run.checkpoint("fair")
    if start <= 1:
        run.wait_control()
        assert run.exit_to("b701"), "lower square to the bell plaza"
        run.wait_control()
        assert run.exit_to("0800"), "bell plaza to the telepod exhibit"
        run.checkpoint("exhibit")
    if start <= 2:
        run.wait_control()                      # Taban's speech
        run.go(183, 97, tolerance=3)            # onto the right telepod
        machine.run(2, "UP")
        machine.run(8)
        for _ in range(10):
            machine.run(4, "A")
            machine.run(60)
            if run.map_id() != "0800":
                break
        run.checkpoint("shell")
    if start <= 3:
        run.fight("crono_and_marle", lambda r, s: r.map_id() == "da01", party=2, seed=args.seed)
        run.checkpoint("cavern")
    if start <= 4:
        run.wait_control()
        assert run.exit_to("db01"), "the inner cavern's second chamber"
        run.checkpoint("inner")
    if start <= 5:
        run.fight("crono_and_marle", lambda r, s: r.map_id() == "df01", party=2, seed=args.seed)
        run.checkpoint("core")
    if start <= 6:
        def core_beaten(r, state):
            return state.max_hp(9) == 30000 and state.hp(9) == 0 and (state.hp(0) or state.hp(1))
        run.fight("crono_and_marle_core", core_beaten, party=2, seed=args.seed)
        run.checkpoint("ending")
    if start <= 7:
        for _ in range(400):
            run.close_dialogs(600)
            if run.map_id() == "7b01" and run.find_player() is not None:
                break
            machine.run(60)
        run.checkpoint("devroom")
    if start <= 8:
        for room, people in DEV_ROOMS:
            if all(bit and (developers() & bit) for _, bit in people):
                continue
            assert run.exit_to(room), room
            for index, bit in people:
                run.talk(index, done=(lambda b=bit: developers() & b) if bit else None)
            assert run.exit_to("7b01"), "back to the pillars"
        run.wait_control()
        assert run.go(494, 353, tolerance=3)    # by Kokubo, where the statue is reachable
        assert run.go(73, 105, tolerance=2)     # the reaper statue, off the platform's edge
        machine.run(2, "RIGHT")
        machine.run(6)
        machine.run(3, "A")
        for _ in range(30):
            machine.run(40)
            if developers() & 0x04:
                break
            machine.run(3, "A")
        run.close_dialogs()
        run.say("developers %02X" % developers())
        assert developers() == 0x7F, "all seven rounds"
        run.checkpoint("tour")
    if start <= 9:
        run.wait_control()
        assert run.go(494, 353, tolerance=3)
        machine.run(2, "LEFT")
        machine.run(8)
        for _ in range(10):
            machine.run(3, "A")
            machine.run(40)
            if developers() == 0xFF:
                break
        run.close_dialogs()
        assert developers() == 0xFF, "Kokubo opens the Gates"
        run.checkpoint("kokubo")
    if start <= 10:
        assert run.exit_other(DEV_ROOM_MAPS), "the Dream Team room"
        for index, bit in DREAM_TEAM:
            run.talk(index, done=lambda b=bit: dream_team() & b)
        run.say("dream team %02X" % dream_team())
        assert dream_team() == 0x1F, "all five of the Dream Team"
        run.dialogs_until(lambda: False, 3000)  # the credits run themselves
        run.checkpoint("credits")
    machine.screenshot(run.prefix + "_end.png")
    run.say("end frame", machine.frame)
    run.finish("the Dream Project ending")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
