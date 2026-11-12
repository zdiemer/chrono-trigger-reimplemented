"""Saved-game projection used by native outcome checks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# --end-state's layout: work RAM, then save RAM.
WRAM_BYTES, SRAM_BYTES = 0x20000, 0x2000
# The save code's table of what a saved game holds ($FF:FA7B, file offset):
# eight runs of work RAM, five bytes each, a 24-bit address and a length.
SAVED_RUNS_AT, SAVED_RUNS, SAVED_RUN_BYTES = 0x3FFA7B, 8, 5
# Save RAM: three slots of a saved game each (menu/saves.h), and the slots'
# sums, which cover the timed cells too.
SAVE_SLOTS, SAVED_GAME_BYTES, SLOT_SUMS = 3, 0xA00, range(0x1FF0, 0x1FF6)
# The saved cells the game's own timing writes, left out: the play clock
# ($7E:0400, six digits, a frame a tick), and the menus' frame count as they
# left for the game ($7E:29AD), which the battles start their random numbers
# from.
TIMED_CELLS = set(range(0x0400, 0x0406)) | {0x29AD}


class SavedGame:
    """Which bytes of the end state are compared: the work RAM offsets a saved
    game holds, and the save RAM offsets, each without the timed cells."""

    def __init__(self, rom):
        table = rom.read_bytes()[SAVED_RUNS_AT:SAVED_RUNS_AT + SAVED_RUNS * SAVED_RUN_BYTES]
        held = []
        for i in range(SAVED_RUNS):
            run = table[i * SAVED_RUN_BYTES:(i + 1) * SAVED_RUN_BYTES]
            addr, bank, length = run[0] | run[1] << 8, run[2], run[3] | run[4] << 8
            base = (bank - 0x7E) << 16 if bank in (0x7E, 0x7F) else 0   # bank $00's low 8 KiB is work RAM's
            held += [base + addr + k for k in range(length)]
        timed_places = {place for place, o in enumerate(held) if o in TIMED_CELLS}
        self.wram = [o for o in held if o not in TIMED_CELLS]
        left_out = {slot * SAVED_GAME_BYTES + place for slot in range(SAVE_SLOTS) for place in timed_places}
        left_out |= set(SLOT_SUMS)
        self.sram = [o for o in range(SRAM_BYTES) if o not in left_out]



def seconds_for(frames):
    return 120 + frames // 50
