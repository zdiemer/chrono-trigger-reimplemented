"""The RAM map (re/ram.toml) and the header generated from it (game/ram.h).

The header is committed so that a build without a ROM has it; these tests fail
when it is out of date with the map (regenerate with `python3 tools/ram_map.py
header`), and check the map's own rules on synthetic maps.
"""
import unittest

from tools import ram_map
from tools.movies import addresses


PAGES = """
[[page]]
name = "known"
d = 0x0100
subsystem = "field"
meaning = "m"
source = "s"

[[page]]
name = "unknown"
subsystem = "core"
meaning = "m"
source = "s"
why = "w"
"""


def var(name, extra, subsystem="field"):
    return '\n[[var]]\nname = "%s"\nsubsystem = "%s"\nmeaning = "m"\nsource = "s"\n%s\n' % (
        name, subsystem, extra)


class TestCommittedMap(unittest.TestCase):
    def test_map_is_valid(self):
        ram_map.RamMap()

    def test_header_is_current(self):
        m = ram_map.RamMap()
        current = ram_map.HEADER.read_text(encoding="utf-8")
        self.assertEqual(current, ram_map.render_header(m),
                         "game/ram.h is out of date with re/ram.toml: "
                         "run python3 tools/ram_map.py header")

    def test_movie_tool_addresses_are_in_the_map(self):
        """tools/movies/addresses.py names the same memory: the two must agree."""
        m = ram_map.RamMap()
        for name in dir(addresses):
            value = getattr(addresses, name)
            if not name.isupper() or not isinstance(value, (int, tuple)):
                continue
            addr = value[0] if isinstance(value, tuple) else value
            if addr >> 16 not in (0x7E, 0x7F) or addr in ram_map.BANK_BASES:
                continue   # strides, sizes, ROM tables
            with self.subTest(name=name):
                self.assertTrue(m.lookup(addr), "%s $%06X is not in re/ram.toml" % (name, addr))


class TestRules(unittest.TestCase):
    def load(self, text):
        return ram_map.RamMap(text=PAGES + text)

    def test_scalar_array_and_record(self):
        m = self.load(var("a", 'addr = "7E:1000"\nsize = 2')
                      + var("b", 'addr = "7E:1100"\ncount = 4\nstride = 2\nindex = "slot * 2"')
                      + var("c", 'addr = "7E:5E30"\ncount = 2\nstride = 0x80\nindex = "slot"\n'
                                 '[[var.field]]\nname = "hp"\noffset = 0\nsize = 2\nmeaning = "m"\n'
                                 '[[var.field]]\nname = "mp"\noffset = 4\nsize = 2\nmeaning = "m"', "battle"))
        v, f = m.lookup(0x7E5EB4)[0]
        self.assertEqual((v.name, f.name), ("c", "mp"))
        v, f = m.lookup(0x7E5E38)[0]                   # inside the record, where no field is named
        self.assertEqual((v.name, f), ("c", None))
        self.assertEqual(m.lookup(0x7E1106)[0][0].name, "b")
        self.assertFalse(m.lookup(0x7E1108))
        text = ram_map.render_header(m)
        self.assertIn("#define RAM_C_MP", text)
        self.assertIn("#define RAM_B_STRIDE 0x02u", text)

    def test_direct_page_known_and_unknown(self):
        m = self.load(var("obj", 'addr = "7E:016D"\npage = "known"\nsize = 2')
                      + var("pad", 'page = "unknown"\ndp = 0xF2\nsize = 2', "core"))
        text = ram_map.render_header(m)
        self.assertIn("#define RAM_OBJ 0x7E016Du", text)
        self.assertIn("#define RAM_OBJ_DP 0x6Du", text)
        self.assertIn("#define RAM_PAD_DP 0xF2u", text)
        self.assertNotIn("#define RAM_PAD ", text)
        self.assertNotIn("RAM_PAGE_UNKNOWN ", text)

    def test_errors(self):
        bad = {
            "address in the page's range": var("x", 'addr = "7E:0300"\npage = "known"'),
            "no address without D": var("x", 'addr = "7E:0100"\npage = "unknown"', "core"),
            "dp derived when D is known": var("x", 'addr = "7E:0100"\npage = "known"\ndp = 0'),
            "array without index": var("x", 'addr = "7E:1000"\ncount = 2'),
            "name by meaning, lower case": var("X1", 'addr = "7E:1000"'),
            "bank": var("x", 'addr = "C0:1000"'),
            "same subsystem overlap": var("x", 'addr = "7E:1000"\nsize = 2') + var("y", 'addr = "7E:1001"'),
            "duplicate": var("x", 'addr = "7E:1000"') + var("x", 'addr = "7E:2000"'),
            "generated clash": var("a_b", 'addr = "7E:1000"') + var("a", 'addr = "7E:2000"\ncount = 1\n'
                                                                    '[[var.field]]\nname = "b"\noffset = 0\nmeaning = "m"'),
            "field outside the stride": var("x", 'addr = "7E:1000"\nstride = 4\n'
                                                 '[[var.field]]\nname = "f"\noffset = 3\nsize = 2\nmeaning = "m"'),
            "unknown key": var("x", 'addr = "7E:1000"\nwidth = 2'),
        }
        for why, text in bad.items():
            with self.subTest(why=why), self.assertRaises(ram_map.MapError):
                self.load(text)

    def test_overlap_across_subsystems_is_allowed(self):
        self.load(var("x", 'addr = "7E:2000"\nsize = 4') + var("y", 'addr = "7E:2000"', "battle"))

    def test_interleaved_arrays_do_not_overlap(self):
        self.load(var("x", 'addr = "7E:1800"\ncount = 4\nstride = 2\nindex = "i"')
                  + var("y", 'addr = "7E:1801"\ncount = 4\nstride = 2\nindex = "i"'))


class TestScan(unittest.TestCase):
    def test_comments_and_strings_are_not_uses(self):
        src = 'a(0x1234); /* 0x0127 */ b(0x0128); // 0x0129\n"0x0130"'
        stripped = ram_map.strip_comments(src)
        self.assertNotIn("0x0127", stripped)
        self.assertNotIn("0x0129", stripped)
        self.assertNotIn("0x0130", stripped)
        self.assertIn("0x0128", stripped)


if __name__ == "__main__":
    unittest.main()
