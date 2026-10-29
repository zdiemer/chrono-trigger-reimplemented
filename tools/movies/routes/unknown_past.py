"""The Unknown Past, technoplaza ct-state20 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state20.srm"


def ending(run):
    # $052: story $66 <= progress < $75 sets $1AA to 1 and starts $025.
    # The last interlude ($1AA = 6) calls object 8's Robo dance. That object
    # reveals The End and waits forever at $04A9; object 0 remains at its
    # call-and-wait at $03F2, never reaching the halt after the call.
    watch_ending(run,
                 selected=lambda: run.map_id() == "2500" and run.machine.b(0x7F01AA) == 1,
                 done=lambda: run.map_id() == "2500" and run.machine.b(0x7F01AA) == 6
                 and run.machine.w(0x7E1180) == 0x03F2
                 and run.machine.w(0x7E1190) in (0x04A9, 0x04AA))


def main():
    play_ending(save=SAVE, title="The Unknown Past", ending=ending,
                entry="bucket", party=3, story=0x69)


if __name__ == "__main__":
    raise SystemExit(run_route(main))
