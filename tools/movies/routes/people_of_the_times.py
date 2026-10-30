"""The People of the Times, technoplaza ct-state21 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state21.srm"


def ending(run):
    # $052: story $75 <= progress < $84 starts $05B. The credits continue
    # through $05F, where object 1 loops after the final picture's reveal.
    watch_ending(run,
                 selected=lambda: run.map_id() == "5b00",
                 done=lambda: run.map_id() == "5f00"
                 and run.machine.w(0x7E1182) == 0x035C)


def main():
    # Core seed 1 leaves the reference sound driver waiting forever near
    # the final reveal ($C7:0635). Seed 2 reaches the end; see MOVIES.md.
    play_ending(save=SAVE, title="The People of the Times", ending=ending,
                entry="bucket", party=3, story=0x7E, core_seed_offset=1)


if __name__ == "__main__":
    raise SystemExit(run_route(main))
