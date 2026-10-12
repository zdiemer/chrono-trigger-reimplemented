"""The tech sweep: cast every single and double tech Crono, Magus and Marle
have once, against the two Lavos forms.

Save: zophar chrono-trigger slot 1 (Crono, Magus and Marle inside Lavos's
shell, all techs learned). The fight is long enough, and the party strong
enough, to try every tech list entry the three of them can show without real
risk: docs/movies-wanted.md, "Every tech".
"""
from .. import policies
from ..campaign import Run, route_arguments, run_route

HEADER = """\
# The tech sweep: load the save (Crono, Magus and Marle inside Lavos's shell), walk to the second form's
# chamber, then fight it and the Lavos Core with a policy that walks each member's tech list for the next
# tech not yet cast (singles, then the double techs its party can make) instead of always attacking,
# healing and reviving with items to keep the fight going.
# Save: zophar chrono-trigger (oracle/saves/zophar, zophar.net Chrono Trigger saves), slot 1.
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""

SAVE = "oracle/saves/zophar/chrono-trigger.srm"
SLOT = 0
PHASES = ["inside", "form2", "core"]


def core_beaten(run, state):
    return state.max_hp(9) == 30000 and state.hp(9) == 0 and any(state.hp(slot) for slot in range(3))


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, native=args.native,
              generated=args.generated, sound=args.sound_driver, until=args.until)
    machine = run.machine
    start = run.start_at(PHASES)

    cast_ids = set()
    policy = policies.tech_sweep(cast_ids)
    setup = lambda bot: policies.tracking_do(bot, cast_ids)

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
        machine.run(120)
        run.say("loaded map", run.map_id(), "frame", machine.frame)
        run.checkpoint("inside")
    if start <= 1:
        assert run.exit_to("db01"), "the second form's chamber"
        run.checkpoint("form2")
    if start <= 2:
        run.fight(policy, lambda r, s: r.map_id() == "df01", party=3, seed=args.seed, setup=setup)
        run.say("techs cast after the second form:", len(cast_ids), sorted(cast_ids))
        run.checkpoint("core")
    run.fight(policy, core_beaten, party=3, seed=args.seed, setup=setup)
    run.say("end frame", machine.frame, "techs cast", len(cast_ids), sorted(cast_ids))
    run.finish("the tech sweep")
    if args.movie:
        run.write_movie(args.movie, HEADER.splitlines())
    run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
