"""The Oath, technoplaza ct-state22 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state22.srm"


def ending(run):
    # $052: story $84 <= progress < $8A sets $DF bit 0 and starts $159.
    # $1CC interleaves the credits with Frog's journey and duel. Its last
    # pass sets $DF bit 1 and returns to $0A5 for the castle and The End.
    watch_ending(run,
                 selected=lambda: run.map_id() == "5901" and run.machine.b(0x7F00DF) & 1,
                 done=lambda: run.map_id() == "a500" and run.machine.b(0x7F00DF) & 2
                 and run.machine.b(0x7F00E0) == 8
                 and run.machine.w(0x7E119A) == 0x0579,
                 # The End's palette fade continues after the script halts:
                 # 32 level steps at 15 frames each, plus a final hold.
                 settle_frames=600)


def main():
    play_ending(save=SAVE, title="The Oath", ending=ending,
                entry="bucket", party=3, story=0x87,
                core_policy="party_lavos_core")


if __name__ == "__main__":
    raise SystemExit(run_route(main))
