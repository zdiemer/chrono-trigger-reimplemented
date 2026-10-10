"""The route runner: phases, checkpoints, walking, talking, and fights.

A route (see routes/) is a list of phases. Each one ends with a checkpoint file,
so a route can be resumed at the last phase that finished instead of replaying
the whole run. Inside a phase everything is decided from memory:

    wait_control()   advance dialogue (A only while a window is open) until the
                     pad moves the player again
    exit_to(map)     breadth-first walk until the screen is another map
    go(x, y)         breadth-first walk to a position
    talk(object)     stand next to an object, face it, press A, check a flag
    fight(policy, until)
                     run battles with the bot; every command menu is a
                     checkpoint, and a party wipe backtracks and retries with
                     other choices and a different delay before acting

The route's input is the machine's pad log, so `write_movie()` at the end turns
the whole run into a `.ctm` (docs/MOVIES.md).
"""
import argparse
import os
import random
import time

from . import addresses as A
from . import bfs, mkmovie, policies
from .bot import Bot
from .ctdrv import Machine

PLAYER_SLOT = 4090      # in-memory slot used by find_player
PATH_SLOT = 4089        # in-memory slot used by object_path


REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class RouteDone(Exception):
    """Raised by Run.checkpoint at the phase --until names: the route stops there."""


def route_arguments(description, save=None):
    """The command line every route script takes. `save` is the route's cartridge
    save, relative to the repo (oracle/saves), the default for --sram."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--sram", required=save is None, default=save and os.path.join(REPO, save),
                        help="cartridge save the route starts from" + (" (default %s)" % save if save else ""))
    parser.add_argument("--prefix", default="route", help="where checkpoints, the log and shots go")
    parser.add_argument("--rom", help="ROM (default roms/ct-usa.sfc, or $CT_ROM)")
    parser.add_argument("--drive", help="ct_drive binary (default build/main/ct_drive, or $CT_DRIVE)")
    parser.add_argument("--movie", help="write the finished run as this .ctm")
    parser.add_argument("--seed", type=int, default=1, help="seed for the fights' choices")
    parser.add_argument("--native", choices=("off", "shadow", "on"),
                        help="ct_drive --native (default off, the interpreter; or $CT_DRIVE_NATIVE)")
    parser.add_argument("--recomp", choices=("off", "on", "only"),
                        help="ct_drive --recomp with --native (default on; or $CT_DRIVE_RECOMP)")
    parser.add_argument("--sound-driver", choices=("original", "c"),
                        help="ct_drive --sound-driver, c with --native on (default original; or $CT_DRIVE_SOUND)")
    parser.add_argument("--until", metavar="PHASE", help="stop once this phase's checkpoint is written")
    return parser.parse_args()


def run_route(main):
    """Run a route's main(), turning its outcome into an exit status and a last
    line tools/movies/route_check.py reads: "outcome reached <what> frame <n>"
    (status 0), or "outcome failed <why>" (status 1)."""
    try:
        main()
    except RouteDone as done:
        print("outcome reached %s" % done, flush=True)
        return 0
    except (AssertionError, RuntimeError) as failure:
        print("outcome failed %s: %s" % (type(failure).__name__, failure), flush=True)
        return 1
    return 0


class Run:
    """One route run: a machine, a log, and the helpers above."""

    cell = 6            # BFS cell size in pixels for field maps

    def __init__(self, prefix, rom=None, sram=None, drive=None, native=None, recomp=None, sound=None, until=None):
        self.until = until
        self.prefix = str(prefix)
        os.makedirs(os.path.dirname(self.prefix) or ".", exist_ok=True)
        self.machine = Machine(rom=rom, sram=sram, drive=drive, native=native, recomp=recomp, sound=sound)
        self.logfile = open(self.prefix + ".log", "a", encoding="utf-8")
        self.player = 1

    def close(self):
        self.logfile.close()
        self.machine.close()

    def say(self, *parts):
        message = " ".join(str(part) for part in parts)
        print(message, flush=True)
        self.logfile.write(message + "\n")
        self.logfile.flush()

    # ---- what is on screen ---------------------------------------------------------------

    def map_id(self):
        return self.machine.peek(A.MAP_ID, 2).hex()

    def dialog(self):
        return self.machine.b(A.DIALOG_OPEN) != 0

    def object_pos(self, index):
        machine = self.machine
        return (machine.w(A.OBJECT_X + 2 * index) / 16, machine.w(A.OBJECT_Y + 2 * index) / 16)

    def me(self):
        return self.object_pos(self.player)

    def world_pos(self):
        return (self.machine.w(A.WORLD_X), self.machine.w(A.WORLD_Y))

    # ---- checkpoints ---------------------------------------------------------------------

    def checkpoint(self, name):
        self.machine.save_state("%s_%s.st" % (self.prefix, name))
        self.say("checkpoint", name, "frame", self.machine.frame, "map", self.map_id())
        if name == self.until:
            frame = self.machine.frame
            self.close()
            raise RouteDone("%s frame %d" % (name, frame))

    def finish(self, what):
        """The route reached its goal: say so, with the frame, for route_check."""
        self.say("outcome reached", what, "frame", self.machine.frame)

    def resume(self, name):
        path = "%s_%s.st" % (self.prefix, name)
        if not os.path.exists(path):
            return False
        self.machine.load_state(path)
        self.say("resumed", name, "frame", self.machine.frame)
        return True

    def start_at(self, phases):
        """Index of the first phase to run, resuming the last checkpoint there is."""
        if self.until in phases and self.resume(self.until):
            frame = self.machine.frame
            self.close()
            raise RouteDone("%s frame %d (resumed)" % (self.until, frame))
        for index in range(len(phases) - 1, -1, -1):
            if self.resume(phases[index]):
                return index + 1
        return 0

    # ---- player control ------------------------------------------------------------------

    def find_player(self):
        """The object the pad moves, or None. Sets self.player when it finds one.

        Tested against a no-input baseline in both directions, so an object the
        script happens to walk in a cutscene doesn't pass for the player.

        Not while native routines are suspended (a battle's native code spans
        its frames): no pad moves an object then, and each probe's restore
        would replay from the last base, before the battle (ct_drive), about 20
        seconds each, which made the end of a long fight take hours.
        """
        machine = self.machine
        if machine.native != "off" and machine.native_status().get("suspended", 0):
            return None
        machine.mark(PLAYER_SLOT)
        machine.probe(12)
        base = [self.object_pos(i) for i in range(0x40)]
        moved = {}
        for direction in ("RIGHT", "LEFT", "DOWN", "UP"):
            machine.restore(PLAYER_SLOT)
            machine.probe(12, direction)
            moved[direction] = [self.object_pos(i) for i in range(0x40)]
        machine.restore(PLAYER_SLOT)
        machine.free(PLAYER_SLOT)
        for index in range(0x40):
            if base[index][0] >= 4000:      # off-screen objects park at $FF00
                continue
            horizontal = (moved["RIGHT"][index][0] > base[index][0] and moved["LEFT"][index][0] < base[index][0])
            vertical = (moved["DOWN"][index][1] > base[index][1] and moved["UP"][index][1] < base[index][1])
            if horizontal or vertical:
                self.player = index
                return index
        return None

    def wait_control(self, max_frames=6000, press_every=40):
        """Advance dialogue until the player answers the pad for a few checks running."""
        machine = self.machine
        start = machine.frame
        good = 0
        while machine.frame - start < max_frames:
            if self.dialog():
                good = 0
                machine.run(3, "A")
                machine.run(press_every - 3)
                continue
            if self.find_player() is not None:
                good += 1
                if good >= 4:
                    return True
            else:
                good = 0
            machine.run(30)
        return False

    def close_dialogs(self, max_frames=3000, quiet_frames=40):
        """Press A while windows keep opening, until the screen stays quiet."""
        machine = self.machine
        start = machine.frame
        quiet = 0
        while machine.frame - start < max_frames and quiet < quiet_frames:
            if self.dialog():
                quiet = 0
                machine.run(3, "A")
                machine.run(37)
            else:
                quiet += 5
                machine.run(5)

    def dialogs_until(self, condition, max_frames=20000, press_every=45):
        """Advance dialogue until `condition()`; True when it held."""
        machine = self.machine
        start = machine.frame
        while machine.frame - start < max_frames:
            if condition():
                return True
            if self.dialog():
                machine.run(3, "A")
                machine.run(press_every - 3)
            else:
                machine.run(10)
        return condition()

    # ---- walking -------------------------------------------------------------------------

    def _pos(self):
        index = self.player
        return lambda m: (m.w(A.OBJECT_X + 2 * index) / 16, m.w(A.OBJECT_Y + 2 * index) / 16)

    def _map(self):
        return lambda m: m.peek(A.MAP_ID, 2).hex()

    def walk(self, goal, max_nodes=4000):
        """BFS with the fallbacks that field maps need: held exits, then coarse cells."""
        moves, _ = bfs.bfs_walk(self.machine, goal, self._pos(), self._map(), cell=self.cell,
                               max_nodes=max_nodes)
        if moves is None:
            moves, _ = bfs.bfs_walk(self.machine, goal, self._pos(), self._map(), cell=self.cell,
                                    max_nodes=max_nodes, push=90)
        if moves is None and self.cell < 12:
            moves, _ = bfs.bfs_walk(self.machine, goal, self._pos(), self._map(), cell=12, step=12,
                                    max_nodes=max_nodes)
        return moves

    def go(self, x, y, tolerance=4):
        moves = self.walk(lambda m, px, py: abs(px - x) <= tolerance and abs(py - y) <= tolerance)
        self.say("go", (x, y), "steps", moves and len(moves), "me", self.me())
        return moves is not None

    def exit_to(self, want, rounds=6):
        """Walk until the screen is map `want`, advancing any dialogue on the way."""
        self.wait_control(3000)
        for _ in range(rounds):
            moves = self.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() == want or m.b(A.DIALOG_OPEN) != 0)
            self.machine.run(60)
            self.say("exit_to", want, "steps", moves and len(moves), "now", self.map_id(),
                     "dialog", self.dialog(), "frame", self.machine.frame)
            if self.map_id() == want:
                return True
            if moves is None:
                return False
            self.wait_control()
            if self.map_id() == want:
                return True
        return False

    def exit_other(self, avoid):
        """Walk to any map that isn't this one and isn't in `avoid`."""
        here = self.map_id()
        moves = self.walk(lambda m, px, py: m.peek(A.MAP_ID, 2).hex() not in avoid | {here})
        self.machine.run(60)
        self.say("exit_other", "steps", moves and len(moves), "now", self.map_id(), "frame", self.machine.frame)
        return moves is not None

    def world_go(self, x, y, max_nodes=2500):
        """Overworld walk to a tile."""
        moves, visited = bfs.ow_bfs(self.machine, lambda m, px, py: (px, py) == (x, y),
                                    lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), self._map(), max_nodes=max_nodes)
        self.say("world_go", (x, y), "steps", moves and len(moves), "at", self.world_pos(),
                 "visited", len(visited))
        return moves is not None

    def world_exit(self, avoid=frozenset(), max_nodes=2500):
        """Overworld walk until the screen is a map other than this one and not in `avoid`:
        the field-map exit_other, for a world map's doors into its locations. Presses A at
        every tile tried (`confirm`), since an exit is only entered by standing on it and
        confirming, not by walking onto it."""
        here = self.map_id()
        moves, visited = bfs.ow_bfs(self.machine,
                                    lambda m, px, py: m.peek(A.MAP_ID, 2).hex() not in avoid | {here},
                                    lambda m: (m.w(A.WORLD_X), m.w(A.WORLD_Y)), self._map(), confirm=True,
                                    max_nodes=max_nodes)
        self.machine.run(60)
        self.say("world_exit", "steps", moves and len(moves), "now", self.map_id(),
                 "visited", len(visited), "frame", self.machine.frame)
        return moves is not None

    # ---- the load screen -------------------------------------------------------------------

    def select_slot(self, slot, rounds=4):
        """On the load screen, press DOWN until it is showing save slot `slot` (0-based):
        the cursor skips slots with no save (menu_save_slot_in_use), so which slot it starts
        on depends on what else is on the cartridge."""
        machine = self.machine
        for _ in range(rounds):
            if machine.b(A.LOAD_SLOT_SHOWN) == slot:
                break
            machine.run(2, "DOWN")
            machine.run(30)
        shown = machine.b(A.LOAD_SLOT_SHOWN)
        self.say("select_slot", slot, "shown", shown)
        return shown == slot

    # ---- talking -------------------------------------------------------------------------

    SPOTS = [(0, 18, "UP"), (0, -18, "DOWN"), (-18, 0, "RIGHT"), (18, 0, "LEFT"),
             (0, 26, "UP"), (0, -26, "DOWN"), (-26, 0, "RIGHT"), (26, 0, "LEFT"),
             (0, 12, "UP"), (0, 32, "UP"), (0, -12, "DOWN"), (-12, 0, "RIGHT"), (12, 0, "LEFT")]

    def object_path(self, index, frames=480, step=12):
        """Where object `index` goes over the next `frames`, sampled from the
        machine and put back: no time passes and nothing is logged. An object
        that stands still gives one position, a walking one its whole round."""
        machine = self.machine
        machine.mark(PATH_SLOT)
        path = [self.object_pos(index)]
        for _ in range(frames // step):
            machine.probe(step)
            path.append(self.object_pos(index))
        machine.restore(PATH_SLOT)
        machine.free(PATH_SLOT)
        return path

    def aims(self, index, path):
        """Where to stand beside object `index`: where its walk comes nearest the
        party, then where it is now. One point when it doesn't walk."""
        px, py = self.me()
        near = min(path, key=lambda point: abs(point[0] - px) + abs(point[1] - py))
        now = self.object_pos(index)
        moves = abs(near[0] - now[0]) >= 1 or abs(near[1] - now[1]) >= 1
        return [near, now] if moves else [now]

    def talk(self, index, done=None, presses=8):
        """Stand beside object `index`, face it and press A until `done()` (or a window opens).

        Tries the spots around it in turn: which side an object can be spoken to
        from depends on the map, and some need a second press. An object that
        walks is aimed at where its walk comes nearest the party as well as at
        where it stands now, because the far end of a walk is often somewhere
        the party can't stand beside: the Dream Team room's two pace aisles the
        party can only reach the near end of, so aiming at the object's position
        alone makes talking to them a race with their own step. Where its walk
        goes is a property of its script; where it stands is a frame count. The
        presses then cover a whole pass of a walking object, which can be at the
        far end of its walk when the party arrives beside the near one.
        """
        machine = self.machine
        self.wait_control(1200)             # objects stand still while a window is open
        path = self.object_path(index)
        for dx, dy, facing in self.SPOTS:
            self.wait_control(1200)
            for tx, ty in self.aims(index, path):
                gx, gy = tx + dx, ty + dy
                if self.walk(lambda m, px, py: abs(px - gx) <= 3 and abs(py - gy) <= 3) is None:
                    continue
                break
            else:
                continue
            machine.run(2, facing)
            machine.run(8)
            opened = False
            for attempt in range(presses):
                machine.run(3, "A")
                machine.run(40)
                opened = opened or self.dialog()
                if (done and done()) or (not done and opened):
                    break
                if not done and not opened and attempt >= 1:
                    break
            if opened or (done and done()):
                for _ in range(12):
                    if not done or done():
                        break
                    machine.run(3, "A")
                    machine.run(40)
                self.close_dialogs()
                ok = bool(done()) if done else True
                self.say("talk", index, "spot", (dx, dy, facing), "beside", (tx, ty), "ok", ok,
                         "frame", machine.frame)
                if ok:
                    return True
        self.say("talk", index, "FAILED")
        return False

    # ---- fighting ------------------------------------------------------------------------

    def fight(self, policy, until, party=3, seed=1, delays=(0, 0, 0, 3, 8, 15), max_tries=20000,
              max_frames=400000, idle=None):
        """Fight with the bot until `until(run, state)`; backtrack over wipes.

        Every command menu becomes a checkpoint. A wipe reloads one of the last
        few, reseeds the policy and re-rolls the delay before acting (which
        shifts the fight's timing, so the retry plays out differently).

        `idle` is called when nothing has happened for a while with no menu and
        no window - the Day of Lavos shell uses it to keep walking into Lavos.
        """
        machine = self.machine
        choose = policies.POLICIES[policy] if isinstance(policy, str) else policy
        bot = Bot(machine, party=party, log=self.say)
        rng = random.Random(seed)
        checkpoints, free_slots = [], list(range(4000, 0, -1))
        tries = fails_here = deepest = 0
        pending = None
        failures_in_row = 0
        quiet = 0
        start_frame = machine.frame
        started = time.time()

        def status(state):
            return "hp=%s mp=%s enemies=%s" % (
                [state.hp(slot) for slot in range(party)],
                [state.mp(slot) for slot in range(party)],
                [(slot, state.hp(slot)) for slot in range(3, 11) if 1 < state.max_hp(slot) < 65000])

        def backtrack(reason):
            nonlocal tries, fails_here
            tries += 1
            if tries > max_tries or not checkpoints:
                raise RuntimeError("fight search exhausted after %d tries" % tries)
            fails_here += 1
            index = max(0, len(checkpoints) - (1 + fails_here // 4))
            while index > 0 and checkpoints[index]["tries"] >= 8:
                index -= 1
            while len(checkpoints) > index + 1:
                spent = checkpoints.pop()
                machine.free(spent["slot"])
                free_slots.append(spent["slot"])
            point = checkpoints[index]
            point["tries"] += 1
            machine.restore(point["slot"])
            bot.step(0)
            rng.seed(seed * 1000003 + tries * 7919)
            self.say("%s: back to decision %d frame %d (try %d)" % (reason, index, point["frame"], tries))
            return point

        while True:
            if self.dialog() and bot.state.member >= 0x80:
                bot.step(3, "A")
                bot.step(20)
                quiet = 0
            state = bot.step(1)
            quiet = quiet + 1 if state.member >= 0x80 else 0
            if idle and quiet >= 120 and not self.dialog():
                idle(bot)
                quiet = 0
                state = bot.state
            if machine.frame - start_frame > max_frames:
                raise RuntimeError("fight ran for %d frames without ending" % max_frames)
            if until(self, state):
                for point in checkpoints:
                    machine.free(point["slot"])
                self.say("fight done frame", machine.frame, "tries", tries,
                         "elapsed %.0fs" % (time.time() - started))
                return True
            wiped = (state.member >= 0x80 and not any(state.hp(slot) for slot in range(party))
                     and any(state.max_hp(slot) for slot in range(party)))
            if wiped:
                self.say(machine.frame, "wipe", status(state))
                pending = backtrack("wipe")
                continue
            if bot.menu_ready() or pending is not None:
                if pending is None:
                    slot = free_slots.pop()
                    machine.mark(slot)
                    checkpoints.append({"slot": slot, "frame": machine.frame, "tries": 0})
                    if len(checkpoints) > deepest:
                        deepest = len(checkpoints)
                        fails_here = 0
                pending = None
                if not bot.menu_ready():
                    continue
                delay = rng.choice(delays)
                for _ in range(delay):
                    bot.step(1)
                if not bot.menu_ready():
                    continue
                state = bot.state
                action = choose(bot, state, rng, bot.inventory())
                if failures_in_row >= 2:
                    living = policies.enemies(state)
                    action = ("attack", rng.choice(living) if living else None)
                ok = bot.do(action)
                failures_in_row = 0 if ok else failures_in_row + 1
                self.say(machine.frame, "d%d member=%d +%d %s %s %s"
                         % (len(checkpoints) - 1, state.member, delay, action, "ok" if ok else "FAILED",
                            status(bot.state)))

    # ---- output --------------------------------------------------------------------------

    def write_movie(self, path, header_lines=(), frames=None):
        """Write the run's input as a movie, filling {FRAMES} and {CRC} in the header."""
        if self.machine.native != "off":
            self.say("warning: this run was on native code (--native %s); its movie replays only in that mode "
                     "and build, not with ct_dev --native shadow" % self.machine.native)
        count = frames if frames is not None else self.machine.frame
        crc = mkmovie.sram_crc32(self.machine.sram) if self.machine.sram else ""
        header = [line.replace("{FRAMES}", str(count)).replace("{CRC}", crc) for line in header_lines]
        written = self.machine.write_movie(path, header, count)
        self.say("wrote", path, written, "frames")
        return written
