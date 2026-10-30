"""Memory Lane, technoplaza ct-state25 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state25.srm"


def ending(run):
    # $052: $A2 <= story < $AB selects $0EF and Marle/Lucca's review.
    # Object $0E calls The End's reveal, then loops forever at $059E.
    watch_ending(run,
                 selected=lambda: run.map_id() == "ef00",
                 done=lambda: run.map_id() == "ef00"
                 and run.machine.w(0x7E119C) == 0x059E)


def main():
    play_ending(save=SAVE, title="Memory Lane", ending=ending,
                entry="bucket", party=3, story=0xA2, core_policy="party_lavos_core")


if __name__ == "__main__":
    raise SystemExit(run_route(main))
