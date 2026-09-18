"""Game code stays inside what a 65816 C compiler handles.

The hand ports are meant to compile for the SNES as well as the host, into a
ROM whose routines are the C in game/ (docs/TODO.md, "A SNES ROM compiled from
the hand ports"; CLAUDE.md, "Writing a port from generated code"). This checks
the part of that which is mechanical: no floating point, no 64-bit integers,
and no host C library beyond memcpy, memset and memcmp. Comments and strings
are skipped. What it can't check -- logic kept apart from the original's
registers, stack use, speed -- is the porting rules' job.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = ROOT / "game"

COMMENT_OR_STRING = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)

FORBIDDEN_TYPES = re.compile(r"\b(float|double|u?int64_t|long\s+long)\b")

# The host C library a 65816 build has no use for. memcpy, memset and memcmp stay
# allowed: every 65816 toolchain has them, and a block move is MVN either way.
FORBIDDEN_CALLS = re.compile(
    r"\b(malloc|calloc|realloc|free|printf|fprintf|sprintf|snprintf|vsnprintf|puts|"
    r"fopen|fclose|fread|fwrite|fseek|ftell|strlen|strcpy|strncpy|strcat|strcmp|strncmp|"
    r"strchr|strstr|atoi|strtol|abort|exit|qsort|time|clock|rand)\s*\(")


def sources():
    return sorted(p for p in GAME.rglob("*") if p.suffix in (".c", ".h"))


def code_of(path):
    return COMMENT_OR_STRING.sub(" ", path.read_text(errors="replace"))


def violations(text):
    found = [m.group(1) for m in FORBIDDEN_TYPES.finditer(text)]
    found += [m.group(1) for m in FORBIDDEN_CALLS.finditer(text)]
    return found


class PortSubset(unittest.TestCase):
    def test_game_code_stays_in_the_65816_subset(self):
        bad = {}
        for path in sources():
            found = violations(code_of(path))
            if found:
                bad[str(path.relative_to(ROOT)).replace("\\", "/")] = sorted(set(found))
        self.assertEqual(bad, {}, "outside what a 65816 compiler handles (tests/test_port_subset.py)")

    def test_detector(self):
        self.assertEqual(violations("double x; float y;"), ["double", "float"])
        self.assertEqual(violations("uint64_t a; int64_t b; long long c;"),
                         ["uint64_t", "int64_t", "long long"])
        self.assertEqual(violations("p = malloc(4); printf(s);"), ["malloc", "printf"])
        self.assertEqual(violations("memcpy(d, s, n); memset(d, 0, n); memcmp(a, b, n);"), [])
        self.assertEqual(violations("uint32_t addr; int16_t dx; freeze(); timer = 0;"), [])
        self.assertEqual(violations(code_of_text('/* double */ x = "float"; // uint64_t')), [])


def code_of_text(text):
    return COMMENT_OR_STRING.sub(" ", text)


if __name__ == "__main__":
    unittest.main()
