"""Native route protocol, checkpoints and recorded controller input."""
import atexit
import os
import pathlib
import subprocess

from . import mkmovie
from .mkmovie import controllers_of, name_of, pad_of

REPO = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "roms" / "ct-usa.sfc"
DEFAULT_DRIVE = REPO / "build" / "main" / ("ct_native.exe" if os.name == "nt" else "ct_native")


class DriverError(RuntimeError):
    pass


class Driver:
    """Drive ct_native over stdin/stdout, retaining input for movies and checkpoints."""

    def __init__(self, rom=None, sram=None, drive=None):
        rom = str(rom or os.environ.get("CT_ROM") or DEFAULT_ROM)
        drive = str(drive or os.environ.get("CT_NATIVE") or DEFAULT_DRIVE)
        command = [drive, "--drive", "--assets", os.environ.get("CT_NATIVE_ASSETS") or str(pathlib.Path(drive).with_suffix(".assets")), "--rom", rom]
        if sram:
            command += ["--sram", str(sram)]
        self.rom, self.sram, self.drive = rom, sram, drive
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, bufsize=1)
        self.frame, self.pads = 0, []
        log = os.environ.get("CT_PROTOCOL_LOG")
        self.command_log = open(log, "a", encoding="utf-8", buffering=1) if log else None
        atexit.register(self._abort_if_running)

    def _abort_if_running(self):
        """A failed route may unwind before close(); leave no driver behind."""
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.command_log:
            self.command_log.close()
            self.command_log = None

    # ---- protocol ------------------------------------------------------------------------

    def cmd(self, line):
        """Send one command; return the answer without its "ok"."""
        if self.command_log:
            self.command_log.write("> " + line + "\n")
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        answer = self.process.stdout.readline().strip()
        if self.command_log:
            self.command_log.write("< " + answer + "\n")
        if not answer.startswith("ok"):
            raise DriverError("%s -> %s" % (line, answer or "<eof>"))
        return answer[3:]

    def close(self):
        """Quit and propagate a failed native run."""
        try:
            self.cmd("quit")
        except Exception:
            pass
        status = self.process.wait()
        atexit.unregister(self._abort_if_running)
        if self.command_log:
            self.command_log.close()
            self.command_log = None
        if status:
            raise DriverError("ct_native exited with status %d" % status)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- running -------------------------------------------------------------------------

    @staticmethod
    def _run_command(verb, frames, pad):
        """ct_native's run or runw for a pad word: the second controller's word
        only where it holds a button."""
        first, second = controllers_of(pad)
        return "%s %d %d" % (verb, frames, first) + (" %d" % second if second else "")

    def run(self, frames, buttons=0):
        """Run frames holding `buttons`, and log them as this run's input."""
        pad = pad_of(buttons)
        if frames <= 0:
            return
        self._unkept_check()
        self.cmd(self._run_command("run", frames, pad))
        del self.pads[self.frame:]
        self.pads.extend([pad] * frames)
        self.frame += frames

    def _unkept_check(self):
        """A run logged after frames whose buttons nothing kept -- a probe's
        that no restore undid, or a state's loaded with no `.pads` -- would
        put a gap in the input."""
        if len(self.pads) < self.frame:
            raise DriverError("frames %d-%d were run by a probe and not restored, or loaded from a state with no "
                             "input beside it, so the input has no buttons for them" % (len(self.pads), self.frame - 1))

    def probe(self, frames, buttons=0):
        """Run frames without logging them (searches, which restore afterwards)."""
        self.frame = int(self.cmd(self._run_command("run", frames, pad_of(buttons))))

    def press(self, buttons, hold=3, gap=0):
        self.run(hold, buttons)
        self.run(gap)

    def play_movie(self, path, to):
        """Run to frame `to` with a movie's buttons, logging them."""
        self._unkept_check()
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
        if frames > 0:
            self._unkept_check()
        answer = self.cmd(self._run_command("runw", max(frames, 0), pad))
        if frames > 0:
            del self.pads[self.frame:]
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
        """Read a state file back, and the input that reached it from its
        `.pads` sidecar. A state with no sidecar (one copied without its input) leaves the input unknown, never the last run's: reading the
        machine is fine, but a run logged after it, or the count of the
        frames kept, is a DriverError (_unkept_check)."""
        sidecar = str(path) + ".pads"
        self.frame = int(self.cmd("loadfile %s" % path))
        if not os.path.exists(sidecar):
            self.pads = []
            return self.frame
        with open(sidecar, encoding="utf-8") as handle:
            self.pads = [int(value) for value in handle.read().split()]
        if len(self.pads) != self.frame:
            raise DriverError("%s holds %d frames of input for a state at frame %d" % (sidecar, len(self.pads), self.frame))
        return self.frame

    # ---- output --------------------------------------------------------------------------

    def screenshot(self, path):
        self.cmd("shot %s" % os.path.abspath(path))

    def write_movie(self, path, header_lines=(), frames=None):
        pads = self.pads[: frames if frames is not None else self.frame]
        mkmovie.write_movie(path, pads, header_lines)
        return len(pads)


__all__ = ["Driver", "DriverError", "pad_of", "name_of"]
