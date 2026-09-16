"""Game code reaches the hardware only through the platform services.

Rule 1 of docs/PLATFORM.md: no register constants for $2100-$217F, $4016-$4017,
$4200-$421F (except the multiplier and divider) or $4300-$437F in game/, outside
the files not converted yet. A constant counts as a register address when it
fits in 16 bits, or when its bank byte is one where those addresses are I/O
($00-$3F, $80-$BF). Comments and strings are skipped. The SNES-only escapes
(vid_snes_*) are allowed only where registered, and so are the audio service's
calls below the command interface (au_driver_*), which only the CPU half of the
sound driver and the scene code that sends it messages may use.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = ROOT / "game"

# Files whose ports still write registers themselves. Remove a file once it is
# converted; the test fails if a listed file no longer has register constants.
UNCONVERTED = {
    "attract/main_loop.c",   # $4212 polls in RAM wait loops
    "battle/main_loop.c",    # the debug path's second controller
    "battle/setup.c",        # not a register: $213A is a WRAM destination in bank $7E
    "battle/wait.c",         # $4212 and counter polls in idle entries
    "field/sprites.c",       # direct page $2100 for the WRAM port at $2180
}

# Uses of the SNES-only escapes, per file.
ESCAPES = {
    "battle/frame.c": 1,   # $CF:E338, the transfer described by the table at $9EEA
    "field/display.c": 1,  # $C0:2DF1, which copies a PPU register into memory rather than writing one
    "menu/display.c": 2,   # $C2:9512 and $C2:9542, which fill and copy memory through the work RAM port
    "scene/display.c": 3,  # $7E:3843 and $7E:5AEB, whose transfers no call expresses, and $7E:38E7,
                           # whose mode and destination are game data
}

# Files allowed to use the driver-level audio calls (au_driver_*).
AUDIO_DRIVER = {
    "core/sound_driver.c",   # the CPU half of the sound driver in bank C7
    "scene/program.c",       # the scene program's own send of a driver command, and the race's port writes
}

MATH = set(range(0x4202, 0x4207)) | set(range(0x4214, 0x4218))
COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
HEX = re.compile(r"\b0[xX]([0-9A-Fa-f]+)[uUlL]*\b")


def is_register(value):
    if value > 0xFFFFFF:
        return False
    if value > 0xFFFF and (value >> 16) & 0x40:
        return False
    addr = value & 0xFFFF
    if addr in MATH:
        return False
    return (0x2100 <= addr <= 0x217F or addr in (0x4016, 0x4017) or 0x4200 <= addr <= 0x421F
            or 0x4300 <= addr <= 0x437F)


def strip(source):
    return COMMENT_OR_STRING.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), source)


def register_constants(path):
    text = strip(path.read_text(encoding="utf-8"))
    found = []
    for m in HEX.finditer(text):
        if is_register(int(m.group(1), 16)):
            line = text.count("\n", 0, m.start()) + 1
            found.append(f"{path.relative_to(ROOT).as_posix()}:{line}: {m.group(0)}")
    return found


class PlatformLint(unittest.TestCase):
    def sources(self):
        return sorted(p for p in GAME.rglob("*") if p.suffix in (".c", ".h"))

    def test_no_register_constants_in_converted_files(self):
        problems = []
        for path in self.sources():
            if path.relative_to(GAME).as_posix() not in UNCONVERTED:
                problems += register_constants(path)
        self.assertEqual(problems, [], "register constants outside the platform services")

    def test_unconverted_list_is_current(self):
        for name in sorted(UNCONVERTED):
            path = GAME / name
            self.assertTrue(path.exists(), f"{name} is listed as unconverted but doesn't exist")
            self.assertTrue(register_constants(path), f"{name} has no register constants: remove it from UNCONVERTED")

    def test_escapes_only_where_registered(self):
        counts = {}
        for path in self.sources():
            n = len(re.findall(r"\bvid_snes_\w+\s*\(", strip(path.read_text(encoding="utf-8"))))
            if n:
                counts[path.relative_to(GAME).as_posix()] = n
        self.assertEqual(counts, ESCAPES)

    def test_audio_driver_calls_only_where_allowed(self):
        users = set()
        for path in self.sources():
            if re.search(r"\bau_driver_\w+\s*\(", strip(path.read_text(encoding="utf-8"))):
                users.add(path.relative_to(GAME).as_posix())
        self.assertEqual(users, AUDIO_DRIVER)

    def test_detector(self):
        self.assertTrue(is_register(0x2100))
        self.assertTrue(is_register(0x00420C))
        self.assertTrue(is_register(0x804370))
        self.assertFalse(is_register(0x4202))
        self.assertFalse(is_register(0x2180))
        self.assertFalse(is_register(0x7E2100))
        self.assertFalse(is_register(0xC02100))
        self.assertEqual(strip('a /* 0x2100 */ "0x2100" 0x2101'), "a " + " " * 12 + " " + " " * 8 + " 0x2101")


if __name__ == "__main__":
    unittest.main()
