"""Client for `ct_drive` (dev/ct_drive.c): run the machine, read its memory,
checkpoint it, and keep the input that produced the run.

    machine = Machine(rom="roms/ct-usa.sfc", sram="save.srm")
    machine.run(1620)
    machine.run(5, "START")            # every run() call appends to machine.pads
    machine.peek(addresses.MAP_ID, 2)

`pads[f]` is the pad word held on frame `f`, so a run is a movie: rewinding to a
checkpoint and running again overwrites the input from that frame on, exactly as
a re-recording emulator does. `write_movie()` turns the log into a `.ctm`.

Two levels of checkpoint:

    mark/restore/free   raw machine copies, for searches that throw work away
    save_state/load_state   a file plus its `.pads` sidecar, to resume later

`probe()` runs frames without logging them; searches use it so that only the
path they keep ends up in the movie.
"""
import os
import pathlib
import subprocess

from . import mkmovie
from .mkmovie import name_of, pad_of

REPO = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "roms" / "ct-usa.sfc"
DEFAULT_DRIVE = REPO / "build" / "main" / ("ct_drive.exe" if os.name == "nt" else "ct_drive")


class DriveError(RuntimeError):
    pass


class Machine:
    def __init__(self, rom=None, sram=None, drive=None):
        rom = str(rom or os.environ.get("CT_ROM") or DEFAULT_ROM)
        drive = str(drive or os.environ.get("CT_DRIVE") or DEFAULT_DRIVE)
        command = [drive, rom] + ([str(sram)] if sram else [])
        self.rom, self.sram, self.drive = rom, sram, drive
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1)
        self.frame = 0
        self.pads = []

    # ---- protocol ------------------------------------------------------------------------

    def cmd(self, line):
        """Send one command; return the answer without its "ok"."""
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        answer = self.process.stdout.readline().strip()
        if not answer.startswith("ok"):
            raise DriveError("%s -> %s" % (line, answer or "<eof>"))
        return answer[3:]

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.process.wait()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- running -------------------------------------------------------------------------

    def run(self, frames, buttons=0):
        """Run frames holding `buttons`, and log them as this run's input."""
        pad = pad_of(buttons)
        if frames <= 0:
            return
        self.cmd("run %d %d" % (frames, pad))
        del self.pads[self.frame:]
        while len(self.pads) < self.frame:
            self.pads.append(0)
        self.pads.extend([pad] * frames)
        self.frame += frames

    def probe(self, frames, buttons=0):
        """Run frames without logging them (searches, which restore afterwards)."""
        self.frame = int(self.cmd("run %d %d" % (frames, pad_of(buttons))))

    def press(self, buttons, hold=3, gap=0):
        self.run(hold, buttons)
        self.run(gap)

    def play_movie(self, path, to):
        """Run to frame `to` with a movie's buttons, logging them."""
        answer = self.cmd("movie %s %d" % (path, to))
        pads = mkmovie.load_movie_pads(path, to)
        del self.pads[self.frame:]
        self.pads.extend(pads[self.frame:to])
        self.frame = int(answer)

    # ---- memory --------------------------------------------------------------------------

    def peek(self, addr, length=1):
        return bytes.fromhex(self.cmd("peek %06X %d" % (addr, length)))

    def b(self, addr):
        return self.peek(addr, 1)[0]

    def w(self, addr):
        low, high = self.peek(addr, 2)
        return low | high << 8

    def watch(self, ranges):
        """Set the (address, length) ranges that runw() returns."""
        self.cmd("watch " + " ".join("%06X:%d" % (addr, length) for addr, length in ranges))

    def runw(self, frames, buttons=0):
        """run() and return the watched bytes afterwards."""
        pad = pad_of(buttons)
        answer = self.cmd("runw %d %d" % (max(frames, 0), pad))
        if frames > 0:
            del self.pads[self.frame:]
            while len(self.pads) < self.frame:
                self.pads.append(0)
            self.pads.extend([pad] * frames)
            self.frame += frames
        frame, _, data = answer.partition(" ")
        self.frame = int(frame)
        return bytes.fromhex(data)

    # ---- checkpoints ---------------------------------------------------------------------

    def mark(self, slot):
        """Copy the machine into an in-memory slot (0-4095)."""
        self.cmd("save %d" % slot)

    def restore(self, slot):
        """Restore a slot. Input logged after its frame is dropped by the next run()."""
        self.frame = int(self.cmd("load %d" % slot))
        return self.frame

    def free(self, slot):
        self.cmd("drop %d" % slot)

    def save_state(self, path):
        """Write a state file and the input that reached it (`<path>.pads`)."""
        self.cmd("savefile %s" % path)
        with open(str(path) + ".pads", "w", encoding="utf-8") as handle:
            handle.write(" ".join(str(pad) for pad in self.pads[: self.frame]))

    def load_state(self, path):
        self.frame = int(self.cmd("loadfile %s" % path))
        sidecar = str(path) + ".pads"
        if os.path.exists(sidecar):
            with open(sidecar, encoding="utf-8") as handle:
                self.pads = [int(value) for value in handle.read().split()]
        return self.frame

    # ---- output --------------------------------------------------------------------------

    def screenshot(self, path):
        self.cmd("shot %s" % os.path.abspath(path))

    def write_movie(self, path, header_lines=(), frames=None):
        pads = self.pads[: frames if frames is not None else self.frame]
        mkmovie.write_movie(path, pads, header_lines)
        return len(pads)


__all__ = ["Machine", "DriveError", "pad_of", "name_of"]
