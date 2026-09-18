"""Beyond Time ending, the standard final battle.

Save: technoplaza ct-state15 slot 1 (Crono, Magus and Lucca inside Lavos).

The second Lavos form and the Lavos Core, then the whole ending: the pardon, the
Moonlight Parade, the farewells at the Gate, Marle and the King at Leene's Bell,
the fireworks and the Epoch credits.
"""
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# Beyond Time ending: load the Final Battle save (Crono, Magus, Lucca inside Lavos), walk to the second Lavos form and
# fight it, then the Lavos Core (body, left bit and core), with Luminaire, Lightning 2, Slash, Dark Matter, Dark Mist, Fire 2,
# Flare, Mega Bomb, attacks, a Revive, Full Tonics, Elixirs and MegaElixirs. Ending: Crono's pardon in Guardia's throne
# room, the Moonlight Parade in Leene Square, walking up to the telepods, the farewells and everyone leaving through the
# Gate, walking Marle back down to the King at Leene's Bell, the balloons and new bell, Taban's fireworks, the Epoch staff
# credits and The End screen.
# Save: technoplaza ct-state15 slot 1 (games.technoplaza.net/snes/chronotrigger/savestates.php, Chrono Trigger (U).srm).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/technoplaza/ct-state15.srm"
PHASES = ["inside", "form2", "core", "ending"]
SQUARE, BELL_PLAZA, TELEPODS = "5000", "5100", "5500"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              recomp=args.recomp, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    if start <= 0:
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
        run.checkpoint("inside")
    if start <= 1:
        assert run.exit_to("db01"), "the second form's chamber"
        run.checkpoint("form2")
    if start <= 2:
        run.fight("party", lambda r, s: r.map_id() == "df01", party=3, seed=args.seed)
        run.checkpoint("core")
    if start <= 3:
        def core_beaten(r, state):
            return state.max_hp(9) == 30000 and state.hp(9) == 0 and any(state.hp(slot) for slot in range(3))
        run.fight("party", core_beaten, party=3, seed=args.seed)
        run.checkpoint("ending")

    # The ending hands control back three times: walk up through Leene Square to
    # the telepods, back down after the farewells, and over to the King.
    seen, king_done, idle_since = [], False, machine.frame
    ending_start = machine.frame
    shot_every, last_shot = 2000, -1
    while machine.frame - ending_start < 48000:
        here = run.map_id()
        if not seen or seen[-1] != here:
            seen.append(here)
            run.say("ending map", here, "frame", machine.frame)
        if run.dialog():
            machine.run(3, "A")
            machine.run(37)
            idle_since = machine.frame
        elif machine.frame - idle_since >= 60 and run.find_player() is not None:
            if here == SQUARE:
                run.exit_to(BELL_PLAZA)
            elif here == BELL_PLAZA and TELEPODS not in seen:
                run.exit_to(TELEPODS)
            elif here == TELEPODS:
                run.exit_to(BELL_PLAZA)
            elif here == BELL_PLAZA and not king_done:
                king = min((index for index in range(8, 0x20) if run.object_pos(index)[0] < 4000),
                           key=lambda index: abs(run.object_pos(index)[0] - 344) + abs(run.object_pos(index)[1] - 240))
                run.say("king object", king, run.object_pos(king))
                king_done = run.talk(king)
            else:
                machine.run(3, "A")
                machine.run(30)
            idle_since = machine.frame
        else:
            machine.run(20)
        if machine.frame // shot_every != last_shot:
            last_shot = machine.frame // shot_every
            machine.screenshot("%s_e%06d.png" % (run.prefix, machine.frame))
    machine.screenshot(run.prefix + "_end.png")
    run.say("end frame", machine.frame, "maps", " ".join(seen))
    assert TELEPODS in seen and king_done, "the farewells at the Gate and Marle back to the King"
    run.finish("the Beyond Time ending")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
