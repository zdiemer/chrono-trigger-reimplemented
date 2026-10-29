"""Dino Age, technoplaza ct-state23 slot 2."""
from ..campaign import run_route
from ..other_endings import play_ending, watch_ending

SAVE = "oracle/saves/technoplaza/ct-state23.srm"


def ending(run):
    # $052: $8A <= story < $99 sets $015C = 1 and starts $10F.
    # Its seventh pass drops Ayla and reveals The End, then object 7
    # loops at $03FD waiting for the ending song to finish and restarting it.
    watch_ending(run,
                 selected=lambda: run.map_id() == "0f01" and run.machine.b(0x7F015C) == 1,
                 done=lambda: run.map_id() == "0f01" and run.machine.b(0x7F015C) == 7
                 and run.machine.w(0x7E118E) in (0x03FD, 0x03FE, 0x0400))


def main():
    # Core seed 1 reaches The End but exposes a battle_irq shadow mismatch;
    # seed 2 completes with a clean replay. Keep the original finding in TODO.md.
    play_ending(save=SAVE, title="Dino Age", ending=ending,
                entry="bucket", party=3, story=0x8E, core_policy="party_lavos_core",
                core_seed_offset=1)


if __name__ == "__main__":
    raise SystemExit(run_route(main))
