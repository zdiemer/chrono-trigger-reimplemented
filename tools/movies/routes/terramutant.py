"""Fight the TerraMutant in the Black Omen to where its movie ends: both parts hurt and up, the party standing.

Save: ctsave15 slot 2 (fantasyanime archive; Crono, Marle and Lucca at the Black Omen's
halfway save point).

The walk from the save point to the boss is the movie's own (late-omen-terramutant), which
the field plays on time; the fight is the battle bot's, retried from its command menus where
the party falls. The movie ends partway through the fight, its head down to 6,869 HP of 7,800
and its lower body to 16,923 of 20,000, so the route stops at that point of the fight: the head
hurt at least as much, both parts still up, nobody fallen. Nothing the saved game keeps has
changed by then but what the fight spends, so its facts there (tools/fight_facts.txt) are the
movie's reference end state's.
"""
from pathlib import Path

from .. import addresses as A
from ..campaign import REPO, Run, route_arguments, run_route

SAVE = "saves/fantasyanime/ctsave15-black_omen.srm"
SLOT = 1
MOVIE = Path(REPO) / "movies/saves/late-omen-terramutant.ctm"
PHASES = ["walked", "fought"]
# The movie's walk ends as the battle begins (its first command menu opens at frame 4503 in
# the reference); at its end the head is down to this.
WALK_ENDS = 4380
HEAD, HEAD_AT_THE_END = 3, 6869
FIGHT_FRAMES = 20000
HEADER = """\
# The TerraMutant in the Black Omen: the movie's walk from the halfway save point, then the battle bot's
# fight, stopped where the movie ends, both parts hurt and up, the party standing (route terramutant).
# Save: ctsave15 slot 2 (saves/fantasyanime, fantasyanime.com Chrono Trigger saves).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.play_movie(str(MOVIE), WALK_ENDS)
            assert run.dialogs_until(lambda: m.b(A.BATTLE_MENU_MEMBER) < 3, 3000), "the TerraMutant's battle"
            run.checkpoint("walked")
        if start <= 1:
            def as_far_as_the_movie(r, state):
                parts = [slot for slot in range(3, 11) if state.max_hp(slot) > 1]
                return (all(state.hp(slot) for slot in parts) and 0 < state.hp(HEAD) <= HEAD_AT_THE_END
                        and all(state.hp(i) for i in range(3)))

            run.fight("party", as_far_as_the_movie, seed=args.seed, max_frames=FIGHT_FRAMES)
            run.checkpoint("fought")
        run.finish("the TerraMutant fought to where its movie ends")
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
