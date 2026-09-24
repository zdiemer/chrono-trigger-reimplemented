"""Game code reaches the hardware only through the platform services.

Rule 1 of docs/PLATFORM.md: no register constants for $2100-$217F, $4016-$4017,
$4200-$421F (except the multiplier and divider) or $4300-$437F in game/, outside
the files not converted yet. A constant counts as a register address when it
fits in 16 bits, or when its bank byte is one where those addresses are I/O
($00-$3F, $80-$BF). Comments and strings are skipped. The SNES-only escapes
(vid_snes_*) are allowed only where registered, and so are the audio service's
calls below the command interface (au_driver_*), which only the CPU half of the
sound driver and the scene code that sends it messages may use. The SNES
backend's write orders for transfers and line effects (VID_STEP_*, VidSnesXfer)
are left only in the files that haven't moved to the calls that look them up.
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
}

# Uses of the SNES-only escapes, per file.
ESCAPES = {
    "battle/uploads.c": 1, # $CF:E338, the transfer described by the table at $9EEA
    "field/display.c": 1,  # $C0:2DF1, which copies a PPU register into memory rather than writing one
    "menu/display.c": 2,   # $C2:9512 and $C2:9542, which fill and copy memory through the work RAM port
    "scene/race.c": 4,     # the buffer clears through the multiplier (one call, three sites), the read
                           # back out of VRAM at $7E:3D41, and the column at $7E:4671 and the plane at
                           # $7E:876E, which write the data port's low half alone
    "scene/screen.c": 4,   # $7E:3843 and the two clears at $7E:3092, whose transfers no call expresses,
                           # and $7E:38E7, whose mode and destination are game data
    "scene/title.c": 2,    # $7E:5AEB, which writes the data port's low half alone, and $7E:59D5, which
                           # carries on from what the channel holds
}

# Files whose calls still spell out the SNES backend's write order for a transfer
# or a line effect (VID_STEP_*, VidSnesXfer), which the vid_send_* uploads,
# vid_set_line_slot and vid_snes_copy take from the backend instead
# (platform/snes/encodings.c; docs/TODO.md, Platform). Remove a file once it
# moves; the test fails if a listed file no longer does, and nothing may be added.
WRITE_ORDER = {
    "menu/epoch.c",
    "menu/line_effects.c",
    "menu/map.c",
}

# Files allowed to use the driver-level audio calls (au_driver_*).
AUDIO_DRIVER = {
    "core/sound_driver.c",   # the CPU half of the sound driver in bank C7
    "scene/sound.c",         # the scene program's own send of a driver command
    "scene/race.c",          # the race's engine pitch, written to the driver's ports
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

    def test_write_order_only_where_left(self):
        users = set()
        for path in self.sources():
            if re.search(r"\bVID_STEP_\w+|\bVidSnesXfer\b", strip(path.read_text(encoding="utf-8"))):
                users.add(path.relative_to(GAME).as_posix())
        self.assertEqual(users, WRITE_ORDER)

    def test_sites_have_encodings(self):
        """A call that takes its write order from the SNES backend at a literal
        site has that site in the backend's table, so no movie is needed to find
        one missing (the backend stops the program on an unknown site)."""
        table = (ROOT / "runtime/platform/snes/encodings.c").read_text(encoding="utf-8")
        known = {int(m, 16) for m in re.findall(r"\b0x([0-9A-Fa-f]{6})\b", table)}
        runs = [(int(a, 16), int(n), int(st, 16)) for a, n, st in
                re.findall(r"\{ 0x([0-9A-Fa-f]{6}), ([1-9]\d*), 0x([0-9A-Fa-f]+),", table)]
        call = re.compile(r"\bvid_(?:send_\w+|set_line_slot|set_line_table|snes_copy|apply_screen|start_vram_uploads)"
                          r"\s*\([^,]+,\s*PF_AT\(0x([0-9A-Fa-f]{6})\)")
        missing = []
        for path in self.sources():
            for m in call.finditer(strip(path.read_text(encoding="utf-8"))):
                site = int(m.group(1), 16)
                if "start_vram_uploads" in m.group(0):
                    continue   # a VRAM port on its own needs no encoding
                if site not in known and not any(a <= site < a + n * st and (site - a) % st == 0 for a, n, st in runs):
                    missing.append(f"{path.relative_to(ROOT).as_posix()}: ${site >> 16:02X}:{site & 0xFFFF:04X}")
        self.assertEqual(missing, [], "calls whose site has no encoding in runtime/platform/snes/encodings.c")

    def test_audio_driver_calls_only_where_allowed(self):
        users = set()
        for path in self.sources():
            if re.search(r"\bau_driver_\w+\s*\(", strip(path.read_text(encoding="utf-8"))):
                users.add(path.relative_to(GAME).as_posix())
        self.assertEqual(users, AUDIO_DRIVER)

    def test_no_sound_hardware_in_game(self):
        """The sound driver (game/sound/) plays through the voices (platform/audio.h):
        no S-DSP register names, and neither the emulated S-DSP nor the voices'
        register implementation, which are the host's."""
        problems = []
        hardware = re.compile(r'#\s*include\s*"(apu/[^"]*|platform/voices_dsp\.h)"|\bDSP_[A-Z]')
        for path in self.sources():
            text = path.read_text(encoding="utf-8")
            code = strip(text) + "\n" + "\n".join(re.findall(r'#\s*include\s*"[^"]*"', text))
            for m in hardware.finditer(code):
                problems.append(f"{path.relative_to(ROOT).as_posix()}: {m.group(0)}")
        self.assertEqual(problems, [], "sound hardware outside the platform services")

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
