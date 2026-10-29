"""What the Prophet Seeks, technoplaza ct-state24 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state24.srm"


def ending(run):
    # $052 selects $190 with $F7 bit 7. Its first pass sets $F8 bit 7;
    # $19E later sets bit 6, selecting the final credits/reveal on returning.
    # The last $190 pass clears both bits and halts after The End.
    watch_ending(run,
                 selected=lambda: run.map_id() == "9001"
                 and run.machine.b(0x7F00F7) & 0x80
                 and run.machine.b(0x7F00F8) & 0x80,
                 done=lambda: run.map_id() == "9001"
                 and run.machine.b(0x7F00F8) & 0xC0 == 0
                 and run.machine.w(0x7E1180) == 0x01B6)


def main():
    play_ending(save=SAVE, title="What the Prophet Seeks", ending=ending,
                entry="bucket", party=3, story=0x9F, core_policy="party_lavos_core")


if __name__ == "__main__":
    raise SystemExit(run_route(main))
