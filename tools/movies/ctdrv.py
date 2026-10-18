"""Client for `ct_drive` (dev/ct_drive.c): run the machine, read its memory,
checkpoint it, and keep the input that produced the run.

    machine = Machine(rom="roms/ct-usa.sfc", sram="save.srm")
    machine.run(1620)
    machine.run(5, "START")            # every run() call appends to machine.pads
    machine.run(3, "P2:SELECT+P2:START")   # the second controller's buttons
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
from .mkmovie import controllers_of, name_of, pad_of

REPO = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_ROM = REPO / "roms" / "ct-usa.sfc"
DEFAULT_DRIVE = REPO / "build" / "main" / ("ct_drive.exe" if os.name == "nt" else "ct_drive")


class DriveError(RuntimeError):
    pass


def replay_seconds(frames):
    """How long the replay of a run's kept frames may take before it counts as
    hung: far more than any takes (ct_drive plays well over 60 frames a
    second)."""
    return 300 + frames // 10


class Machine:
    """`native`, `generated` and `sound` are ct_drive's --native, --generated and
    --sound-driver (default: the interpreter and the original sound driver, or
    $CT_DRIVE_NATIVE, $CT_DRIVE_GENERATED and $CT_DRIVE_SOUND). With --native on
    and $CT_DRIVE_FALLBACKS naming a directory, each machine also counts what
    the original's code runs in place of hand ports (ct_drive --fallbacks) into
    a file of its own there, for tools/fallbacks.py: over the frames the run
    kept, its input as close() finds it, played again from the start by a
    ct_drive of its own (which must end with the run's frame and work RAM), so that what searches threw away (probes, the attempts
    a restore undoes, the frames a load replays) isn't counted, and a run
    resumed from a state file is counted from its start. With $CT_DRIVE_DRAIN_BUDGETS
    set and --native on, the field's frame budgets have time for all their work,
    as a host's do (ct_drive --drain-budgets)."""

    _count = 0

    def __init__(self, rom=None, sram=None, drive=None, native=None, generated=None, sound=None):
        rom = str(rom or os.environ.get("CT_ROM") or DEFAULT_ROM)
        drive = str(drive or os.environ.get("CT_DRIVE") or DEFAULT_DRIVE)
        native = native or os.environ.get("CT_DRIVE_NATIVE") or "off"
        generated = generated or os.environ.get("CT_DRIVE_GENERATED") or "on"
        sound = sound or os.environ.get("CT_DRIVE_SOUND") or "original"
        command = [drive]
        if native != "off":
            command += ["--native", native, "--generated", generated]
        if sound != "original":
            command += ["--sound-driver", sound]
        if os.environ.get("CT_DRIVE_DRAIN_BUDGETS") and native == "on":
            command += ["--drain-budgets"]
        fallbacks = os.environ.get("CT_DRIVE_FALLBACKS")
        self.counted = None
        if fallbacks and native == "on":
            Machine._count += 1
            self.counted = (list(command), os.path.join(fallbacks, "drive-%d-%d.txt" % (os.getpid(), Machine._count)))
        command += [rom] + ([str(sram)] if sram else [])
        self.rom, self.sram, self.drive = rom, sram, drive
        self.native, self.generated, self.sound = native, generated, sound
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
        """Quit. ct_drive exits 1 where the run went wrong in a way no answer
        showed (the C sound driver's host dropping a command, say): a DriveError.
        With $CT_DRIVE_FALLBACKS, the frames kept are then counted."""
        ended = None
        if self.counted:
            # the work RAM the run ends with, which the counting's replay must reach
            ended = self.counted[1] + ".ended.wram"
            try:
                self.cmd("wram %s" % ended)
            except Exception:
                ended = None
        try:
            self.cmd("quit")
        except Exception:
            pass
        status = self.process.wait()
        if status:
            raise DriveError("ct_drive exited with status %d (its output says why)" % status)
        if self.counted:
            if ended is None:
                raise DriveError("counting the kept frames: the run's work RAM couldn't be read as it ended")
            self.count_kept_frames(ended)

    def count_kept_frames(self, ended):
        """Play the input the run kept from the start in a ct_drive that counts
        what the original's code runs in place of hand ports, and write its
        counts. The run's own machine should reach every frame it kept as a
        straight run of that input does (a load replays from a base to the
        frame it was saved at, dev/ct_drive.c); the replay is checked against
        it, the frame it ends on and its work RAM against `ended`, the file
        of the run's work RAM as it ended, and a replay that differs is a
        DriveError rather than a count."""
        command, path = self.counted
        self.counted = None
        if len(self.pads) < self.frame:
            raise DriveError("counting the kept frames: frames %d-%d were run but not kept (a probe not restored, "
                             "or a state loaded with no input)" % (len(self.pads), self.frame - 1))
        pads = self.pads[: self.frame]
        lines = []
        start = 0
        while start < len(pads):
            end = start
            while end < len(pads) and pads[end] == pads[start]:
                end += 1
            lines.append(self._run_command("run", end - start, pads[start]))
            start = end
        replayed = path + ".replayed.wram"
        lines += ["frame", "wram %s" % replayed, "quit"]
        counting = subprocess.Popen(command + ["--fallbacks", path, self.rom] + ([str(self.sram)] if self.sram else []),
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            out, _ = counting.communicate("\n".join(lines) + "\n", timeout=replay_seconds(len(pads)))
        except subprocess.TimeoutExpired:
            counting.kill()
            counting.communicate()
            raise DriveError("counting the kept frames: the replay of %d frames took more than %d seconds"
                             % (len(pads), replay_seconds(len(pads))))
        answers = out.splitlines()
        status = counting.returncode
        failed = [answer for answer in answers if not answer.startswith("ok")]
        if status or failed or len(answers) < 3:
            raise DriveError("counting the kept frames: ct_drive exited with status %d%s"
                             % (status, (", answering " + failed[0]) if failed else ""))
        reached = int(answers[-3][3:])
        try:
            same = reached == self.frame and pathlib.Path(replayed).read_bytes() == pathlib.Path(ended).read_bytes()
        finally:
            for name in (replayed, ended):
                if os.path.exists(name):
                    os.remove(name)
        if not same:
            os.remove(path)
            raise DriveError("counting the kept frames: the replay of the kept input %s"
                             % ("ended on frame %d, the run on %d" % (reached, self.frame) if reached != self.frame
                                else "ended with other work RAM than the run did, so it counted another run"))

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- running -------------------------------------------------------------------------

    @staticmethod
    def _run_command(verb, frames, pad):
        """ct_drive's run or runw for a pad word: the second controller's word
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
            raise DriveError("frames %d-%d were run by a probe and not restored, or loaded from a state with no "
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
        `.pads` sidecar. A state with no sidecar (one ct_dev --save-state
        wrote) leaves the input unknown, never the last run's: reading the
        machine is fine, but a run logged after it, or the count of the
        frames kept, is a DriveError (_unkept_check)."""
        sidecar = str(path) + ".pads"
        self.frame = int(self.cmd("loadfile %s" % path))
        if not os.path.exists(sidecar):
            self.pads = []
            return self.frame
        with open(sidecar, encoding="utf-8") as handle:
            self.pads = [int(value) for value in handle.read().split()]
        if len(self.pads) != self.frame:
            raise DriveError("%s holds %d frames of input for a state at frame %d" % (sidecar, len(self.pads), self.frame))
        return self.frame

    def native_status(self):
        """ct_drive's `native` answer as a dict: mode, suspended, replayed, inexact, lost,
        mismatched (dev/ct_drive.c)."""
        words = self.cmd("native").split()
        status = {"mode": words[0]}
        status.update({words[i]: int(words[i + 1]) for i in range(1, len(words) - 1, 2)})
        return status

    # ---- output --------------------------------------------------------------------------

    def screenshot(self, path):
        self.cmd("shot %s" % os.path.abspath(path))

    def write_movie(self, path, header_lines=(), frames=None):
        pads = self.pads[: frames if frames is not None else self.frame]
        mkmovie.write_movie(path, pads, header_lines)
        return len(pads)


__all__ = ["Machine", "DriveError", "pad_of", "name_of"]
