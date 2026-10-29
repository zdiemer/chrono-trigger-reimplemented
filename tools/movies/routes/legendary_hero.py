"""The Legendary Hero, technoplaza ct-state19 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state19.srm"


def ending(run):
    # $052: story $54 <= progress < $66 sets $1AB to 1 and starts $025.
    # Its six interludes advance that flag; the last returns to $025 and
    # leaves object 0 in the wait/jump loop at $042B after the final fade-in.
    watch_ending(run,
                 selected=lambda: run.map_id() == "2500" and run.machine.b(0x7F01AB) == 1,
                 done=lambda: run.map_id() == "2500" and run.machine.b(0x7F01AB) == 6
                 and run.machine.w(0x7E1180) in (0x042B, 0x042C))


def main():
    play_ending(save=SAVE, title="The Legendary Hero", ending=ending,
                entry="bucket", party=3, story=0x5D)


if __name__ == "__main__":
    raise SystemExit(run_route(main))
