"""The RAM map (re/ram.toml) and what is generated from it (game/ram.h,
game/vars.h, runtime/port/vars_pages.h and runtime/port/vars_ram.c).

The generated files are committed so that a build without a ROM has them; these
tests fail when they are out of date with the map (regenerate with `python3
tools/ram_map.py header` and `accessors`), and check the map's own rules on
synthetic maps.
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
instances = [{ name = "low", d = 0x0000 }, { name = "high", d = 0x1E00 }]
"""

# Page kinds of each shape: named instances only, one array, and a mix.
KINDS = """
[[page]]
name = "input"
subsystem = "core"
meaning = "Controller cells"
source = "s"
why = "w"
instances = [{ name = "low", d = 0x0000 }, { name = "sound", d = 0x1E00 }]

[[page]]
name = "record"
subsystem = "menu"
meaning = "A record's page"
source = "s"
why = "w"
instances = [{ name = "slot", base = 0x1800, count = 8, stride = 0x40 }]

[[page]]
name = "math"
subsystem = "scene"
meaning = "The helpers' page"
source = "s"
why = "w"
instances = [{ name = "own", d = 0x0000 }, { name = "channel", base = 0x0A00, count = 2, stride = 0x100 }]
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

    def test_accessors_are_current(self):
        m = ram_map.RamMap()
        for path, text in ((ram_map.VARS_HEADER, ram_map.render_vars_header(m)),
                           (ram_map.PAGES_HEADER, ram_map.render_pages_header(m)),
                           (ram_map.BACKING, ram_map.render_backing(m))):
            with self.subTest(file=path.name):
                self.assertEqual(path.read_text(encoding="utf-8"), text,
                                 "%s is out of date with re/ram.toml: "
                                 "run python3 tools/ram_map.py accessors" % path.name)

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
        self.assertIn("low (D = $0000); high (D = $1E00)", text)

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
            "past the end of a page kind": var("x", 'page = "unknown"\ndp = 0xFF\nsize = 2', "core"),
        }
        for why, text in bad.items():
            with self.subTest(why=why), self.assertRaises(ram_map.MapError):
                self.load(text)

    def test_overlap_across_subsystems_is_allowed(self):
        self.load(var("x", 'addr = "7E:2000"\nsize = 4') + var("y", 'addr = "7E:2000"', "battle"))

    def test_reuses_names_one_or_several(self):
        """Within a subsystem an overlap needs `reuses`, naming the other or a
        list of the others where the bytes are more than one's."""
        with self.assertRaises(ram_map.MapError):
            self.load(var("x", 'addr = "7E:2000"\nsize = 4') + var("y", 'addr = "7E:2000"'))
        self.load(var("x", 'addr = "7E:2000"\nsize = 4') + var("y", 'addr = "7E:2000"\nreuses = "x"'))
        with self.assertRaises(ram_map.MapError):
            self.load(var("x", 'addr = "7E:2000"') + var("y", 'addr = "7E:2002"')
                      + var("z", 'addr = "7E:2000"\nsize = 4\nreuses = "x"'))
        self.load(var("x", 'addr = "7E:2000"') + var("y", 'addr = "7E:2002"')
                  + var("z", 'addr = "7E:2000"\nsize = 4\nreuses = ["x", "y"]'))

    def test_interleaved_arrays_do_not_overlap(self):
        self.load(var("x", 'addr = "7E:1800"\ncount = 4\nstride = 2\nindex = "i"')
                  + var("y", 'addr = "7E:1801"\ncount = 4\nstride = 2\nindex = "i"'))

    def test_tables_of_rows_interleave(self):
        """Tables whose rows lie between each other's, a row every 256 bytes (the
        field's map tables), don't overlap; one that reaches into another's row does."""
        m = self.load(var("maps", 'addr = "7E:3000"\nsize = 0x40\ncount = 3\nstride = 0x40\nindex = "layer"\n'
                                  'rows = 64\nrow_stride = 0x100')
                      + var("props", 'addr = "7E:30C0"\nsize = 0x40\nrows = 64\nrow_stride = 0x100'))
        self.assertEqual(m.lookup(0x7E3141)[0][0].name, "maps")     # row 1, the second layer's column 1
        self.assertEqual(m.lookup(0x7E31C5)[0][0].name, "props")    # row 1, column 5
        self.assertFalse(m.lookup(0x7E7000))                       # past the last row
        with self.assertRaises(ram_map.MapError):
            self.load(var("maps", 'addr = "7E:3000"\nsize = 0x40\nrows = 4\nrow_stride = 0x100')
                      + var("more", 'addr = "7E:3130"\nsize = 0x20\nrows = 4\nrow_stride = 0x100'))

    def test_names_by_meaning(self):
        """A record's field named for the record it sits in stutters, and is
        rejected; names carrying an address are listed for a meaning."""
        with self.assertRaises(ram_map.MapError):
            self.load(var("object_mode", 'addr = "7E:1000"\ncount = 2\nstride = 2\nindex = "slot"\n'
                                         '[[var.field]]\nname = "mode"\noffset = 0\nmeaning = "m"'))
        m = self.load(var("setting_1e", 'addr = "7E:1000"') + var("setting_bc", 'addr = "7E:1001"')
                      + var("screen_shake", 'addr = "7E:1002"') + var("bg3_scroll", 'addr = "7E:1003"'))
        self.assertEqual(m.address_shaped(), ["setting_1e", "setting_bc"])

    def test_table_of_rows_errors(self):
        bad = {
            "rows without row_stride": var("x", 'addr = "7E:1000"\nsize = 4\nrows = 2'),
            "a row wider than its stride": var("x", 'addr = "7E:1000"\nsize = 0x80\nrows = 2\nrow_stride = 0x40'),
            "fields": var("x", 'addr = "7E:1000"\nstride = 4\nrows = 2\nrow_stride = 0x10\n'
                              '[[var.field]]\nname = "f"\noffset = 0\nmeaning = "m"'),
            "past the bank": var("x", 'addr = "7E:F000"\nsize = 0x40\nrows = 64\nrow_stride = 0x100'),
        }
        for why, text in bad.items():
            with self.subTest(why=why), self.assertRaises(ram_map.MapError):
                self.load(text)


class TestAccessors(unittest.TestCase):
    """game/vars.h: a value per variable, an index per element, a pair per field,
    and no layout -- an address appears only in the backing under runtime/."""

    def load(self, text):
        return ram_map.RamMap(text=PAGES + text)

    def test_shapes(self):
        m = self.load(var("flag", 'addr = "7E:1000"')
                      + var("counter", 'addr = "7E:1002"\nsize = 2')
                      + var("pointer", 'addr = "7E:1004"\nsize = 3')
                      + var("facing", 'addr = "7E:1100"\nsize = 2\ncount = 4\nstride = 2\nindex = "object slot"')
                      + var("name", 'addr = "7E:1200"\nsize = 6')
                      + var("combatant", 'addr = "7E:5E30"\ncount = 2\nstride = 0x80\nindex = "battle slot"\n'
                                         '[[var.field]]\nname = "hp"\noffset = 0\nsize = 2\nmeaning = "m"\n'
                                         '[[var.field]]\nname = "level"\noffset = 4\nmeaning = "m"', "battle"))
        h = ram_map.render_vars_header(m)
        for line in ("uint8_t flag(const GameState *g);",
                     "void set_flag(GameState *g, uint8_t v);",
                     "uint16_t counter(const GameState *g);",
                     "uint32_t pointer(const GameState *g);",
                     "enum { FACING_COUNT = 4 };",
                     "uint16_t facing(const GameState *g, unsigned slot);",
                     "void set_facing(GameState *g, unsigned slot, uint16_t v);",
                     "enum { NAME_BYTES = 6 };",
                     "uint8_t name_byte(const GameState *g, unsigned at);",
                     "enum { COMBATANT_COUNT = 2 };",
                     "uint16_t combatant_hp(const GameState *g, unsigned slot);",
                     "uint8_t combatant_level(const GameState *g, unsigned slot);"):
            with self.subTest(line=line):
                self.assertIn(line, h)

    def test_a_table_of_rows_is_a_byte_by_row_and_column(self):
        m = self.load(var("maps", 'addr = "7E:3000"\nsize = 0x40\ncount = 3\nstride = 0x40\nindex = "layer"\n'
                                  'rows = 64\nrow_stride = 0x100'))
        h = ram_map.render_vars_header(m)
        for line in ("enum { MAPS_ROWS = 64, MAPS_COLUMNS = 64 };",
                     "uint8_t maps(const GameState *g, unsigned layer, unsigned row, unsigned column);",
                     "void set_maps(GameState *g, unsigned layer, unsigned row, unsigned column, uint8_t v);"):
            with self.subTest(line=line):
                self.assertIn(line, h)
        self.assertIn('{ IN_RANGE(g, 0, "maps", "count", layer, 3u); IN_RANGE(g, 0, "maps", "rows", row, 64u); '
                      'IN_RANGE(g, 0, "maps", "columns", column, 64u); '
                      'return r8(g, 0x7E3000u + (uint32_t)layer * 0x40u + (uint32_t)row * 0x100u + column); }',
                      ram_map.render_backing(m))

    def test_the_interface_has_no_layout(self):
        """No address, offset or stride reaches game/vars.h: that is the point of it."""
        m = self.load(var("facing", 'addr = "7E:1100"\nsize = 2\ncount = 4\nstride = 2\nindex = "object slot"'))
        self.assertNotIn("0x7E1100", ram_map.render_vars_header(m))
        self.assertIn("0x7E1100u", ram_map.render_backing(m))

    def test_the_backing_reads_and_writes_the_original_addresses(self):
        m = self.load(var("flag", 'addr = "7E:1000"')
                      + var("pointer", 'addr = "7E:1004"\nsize = 3')
                      + var("facing", 'addr = "7E:1100"\nsize = 2\ncount = 4\nstride = 2\nindex = "object slot"')
                      + var("name", 'addr = "7E:1200"\nsize = 6'))
        c = ram_map.render_backing(m)
        for line in ("uint8_t flag(const GameState *g) { return r8(g, 0x7E1000u); }",
                     "void set_pointer(GameState *g, uint32_t v) { w24(g, 0x7E1004u, v); }",
                     "uint16_t facing(const GameState *g, unsigned slot) { IN_RANGE(g, 0, \"facing\", \"count\", slot, 4u);"
                     " return r16(g, 0x7E1100u + (uint32_t)slot * 0x02u); }",
                     "uint8_t name_byte(const GameState *g, unsigned at)"
                     " { IN_RANGE(g, 1, \"name_byte\", \"bytes\", at, 6u); return r8(g, 0x7E1200u + at); }"):
            with self.subTest(line=line):
                self.assertIn(line, c)

    def test_every_index_is_checked(self):
        """Each index an accessor takes is checked against the map's count, bytes,
        rows and columns, before the address is formed; a scalar has none. The
        check is compiled only with CT_ACCESSOR_CHECKS, and is nothing without."""
        m = self.load(var("flag", 'addr = "7E:1000"')
                      + var("facing", 'addr = "7E:1100"\nsize = 2\ncount = 4\nstride = 2\nindex = "object slot"')
                      + var("combatant", 'addr = "7E:5E30"\ncount = 2\nstride = 0x80\nindex = "battle slot"\n'
                                         '[[var.field]]\nname = "hp"\noffset = 0\nsize = 2\nmeaning = "m"', "battle"))
        c = ram_map.render_backing(m)
        for line in ("void set_flag(GameState *g, uint8_t v) { w8(g, 0x7E1000u, v); }",
                     'void set_facing(GameState *g, unsigned slot, uint16_t v)'
                     ' { IN_RANGE(g, 0, "set_facing", "count", slot, 4u); w16(g, 0x7E1100u + (uint32_t)slot * 0x02u, v); }',
                     'uint16_t combatant_hp(const GameState *g, unsigned slot) { IN_RANGE(g, 1, "combatant_hp", "count", slot, 2u);',
                     '    { .var = "facing" },\n    { .var = "combatant" },\n};',
                     "    *count = 2;",
                     "#define IN_RANGE(g, var, accessor, bound, index, limit) ((void)0)"):
            with self.subTest(line=line):
                self.assertIn(line, c)
        on = c.index("#ifdef CT_ACCESSOR_CHECKS")
        off = c.index("#else", on)
        self.assertLess(on, c.index("static void out_of_range("))
        self.assertLess(c.index("static void out_of_range("), off)

    def test_a_deliberate_stray_access_has_an_unchecked_pair(self):
        """Marked scaffolding that reaches past a variable on purpose uses its
        `_unchecked` pair, which the map declares with a reason; the checked
        pair is still there for everything else."""
        m = self.load(var("facing", 'addr = "7E:1100"\nsize = 2\ncount = 4\nstride = 2\nindex = "object slot"\n'
                                    'unchecked = "the original writes the slot after the last"'))
        h, c = ram_map.render_vars_header(m), ram_map.render_backing(m)
        self.assertIn("void set_facing(GameState *g, unsigned slot, uint16_t v);", h)
        self.assertIn("void set_facing_unchecked(GameState *g, unsigned slot, uint16_t v);", h)
        self.assertIn("Unchecked, for marked scaffolding only", h)
        self.assertIn("void set_facing_unchecked(GameState *g, unsigned slot, uint16_t v)"
                      " { w16(g, 0x7E1100u + (uint32_t)slot * 0x02u, v); }", c)
        m = self.load(var("name", 'addr = "7E:1200"\nsize = 6\nunchecked = "w"'))   # a buffer: one _BYTES
        h = ram_map.render_vars_header(m)
        self.assertEqual(h.count("NAME_BYTES"), 1)
        self.assertIn("uint8_t name_byte_unchecked(const GameState *g, unsigned at);", h)
        for why, text in {"a scalar has no index": var("flag", 'addr = "7E:1000"\nunchecked = "w"'),
                          "unchecked says why": var("facing", 'addr = "7E:1100"\ncount = 4\nindex = "slot"\n'
                                                              'unchecked = ""')}.items():
            with self.subTest(why=why), self.assertRaises(ram_map.MapError):
                self.load(text)

    def test_variables_on_a_page_kind_take_the_instance(self):
        m = self.load(var("held", 'page = "unknown"\ndp = 0xF2\nsize = 2', "core"))
        self.assertIn("uint16_t held(const GameState *g, UnknownPage page);", ram_map.render_vars_header(m))

    def test_a_generated_name_twice_is_an_error(self):
        """A record's field and a variable can spell the same accessor."""
        with self.assertRaises(ram_map.MapError):
            self.load(var("a_b", 'addr = "7E:1000"')
                      + var("a", 'addr = "7E:2000"\n[[var.field]]\nname = "b"\noffset = 0\nmeaning = "m"'))


class TestPageKinds(unittest.TestCase):
    """Cells on a page the caller chooses: the accessor takes which instance of
    the page is meant, and only the backing knows where each instance is."""

    def load(self, text=""):
        return ram_map.RamMap(text=PAGES + KINDS + text)

    VARS = (var("pressed", 'page = "input"\ndp = 0xF0\nsize = 2', "core")
            + var("mapped", 'page = "input"\ndp = 0xF6\nsize = 2\ncount = 3\nindex = "new, held, repeating"', "core")
            + var("record_y", 'page = "record"\ndp = 0x0E', "menu")
            + var("scratch", 'page = "math"\ndp = 0xF0\nsize = 16', "scene"))

    def test_handles(self):
        h = ram_map.render_vars_header(self.load(self.VARS))
        for line in ("typedef enum { INPUT_LOW, INPUT_SOUND } InputPage;",
                     "enum { RECORD_COUNT = 8 };   /* by slot */",
                     "typedef enum { MATH_OWN, MATH_CHANNEL } MathPageKind;",
                     "} MathPage;",
                     "enum { MATH_CHANNEL_COUNT = 2 };",
                     "static inline MathPage math_own(void) { return (MathPage){ MATH_OWN, 0 }; }",
                     "static inline MathPage math_channel(unsigned channel)"
                     " { return (MathPage){ MATH_CHANNEL, (uint8_t)channel }; }"):
            with self.subTest(line=line):
                self.assertIn(line, h)

    def test_accessors_take_the_instance(self):
        h = ram_map.render_vars_header(self.load(self.VARS))
        for line in ("uint16_t pressed(const GameState *g, InputPage page);",
                     "void set_pressed(GameState *g, InputPage page, uint16_t v);",
                     "uint16_t mapped(const GameState *g, InputPage page, unsigned i);",
                     "uint8_t record_y(const GameState *g, unsigned slot);",
                     "void set_record_y(GameState *g, unsigned slot, uint8_t v);",
                     "uint8_t scratch_byte(const GameState *g, MathPage page, unsigned at);"):
            with self.subTest(line=line):
                self.assertIn(line, h)

    def test_the_interface_has_no_d(self):
        """Neither a D nor an offset reaches game/vars.h; the D -> instance
        mapping is in the backing, for entry wrappers."""
        h = ram_map.render_vars_header(self.load(self.VARS))
        for layout in ("0x1E00", "0x1800", "0x0A00", "0x40", "0xF0", "_page_of("):
            with self.subTest(layout=layout):
                self.assertNotIn(layout, h)

    def test_the_backing_reaches_each_instance(self):
        c = ram_map.render_backing(self.load(self.VARS))
        for line in ("    case INPUT_SOUND:\n        return 0x7E1E00u;",
                     "static uint32_t record_page_base(unsigned slot) { return 0x7E1800u + (uint32_t)slot * 0x40u; }",
                     "    case MATH_CHANNEL:\n        return 0x7E0A00u + (uint32_t)page.index * 0x100u;",
                     "uint16_t pressed(const GameState *g, InputPage page) { return r16(g, input_page_base(page) + 0xF0u); }",
                     "uint16_t mapped(const GameState *g, InputPage page, unsigned i)"
                     " { IN_RANGE(g, 0, \"mapped\", \"count\", i, 3u);"
                     " return r16(g, input_page_base(page) + 0xF6u + (uint32_t)i * 0x02u); }",
                     "uint8_t record_y(const GameState *g, unsigned slot)"
                     " { IN_RANGE(g, 1, \"record_y\", \"instances\", slot, 8u);"
                     " return r8(g, record_page_base(slot) + 0x0Eu); }",
                     "uint8_t scratch_byte(const GameState *g, MathPage page, unsigned at)"
                     " { IN_RANGE(g, 2, \"scratch_byte\", \"instances\", page.index, math_page_count(page));"
                     " IN_RANGE(g, 2, \"scratch_byte\", \"bytes\", at, 16u);"
                     " return r8(g, math_page_base(page) + 0xF0u + at); }",
                     "static unsigned math_page_count(MathPage page)\n{\n    switch (page.kind) {\n"
                     "    case MATH_OWN:\n        return 1u;\n    case MATH_CHANNEL:\n        return 2u;"):
            with self.subTest(line=line):
                self.assertIn(line, c)

    def test_the_mapping_from_d(self):
        m = self.load(self.VARS)
        h = ram_map.render_pages_header(m)
        for line in ("bool input_page_of(uint16_t d, InputPage *page);",
                     "bool record_page_of(uint16_t d, unsigned *slot);",
                     "bool math_page_of(uint16_t d, MathPage *page);",
                     "const VarsPageRefusals *vars_page_refusals(unsigned *count);"):
            with self.subTest(line=line):
                self.assertIn(line, h)
        c = ram_map.render_backing(m)
        for line in ("    if (d == 0x1E00u) {\n        *page = INPUT_SOUND;",
                     "    if (d >= 0x1800u && d <= 0x19C0u && (unsigned)(d - 0x1800u) % 0x40u == 0) {\n"
                     "        *slot = (unsigned)(d - 0x1800u) / 0x40u;",
                     "        *page = (MathPage){ MATH_CHANNEL, (uint8_t)((unsigned)(d - 0x0A00u) / 0x100u) };",
                     "    return refuse(2, d);"):
            with self.subTest(line=line):
                self.assertIn(line, c)

    def test_lookup_finds_every_instance(self):
        m = self.load(self.VARS)
        for a in (0x7E00F4, 0x7E0AF4, 0x7E0BF4):
            with self.subTest(addr=hex(a)):
                self.assertEqual([v.name for v, _ in m.lookup(a, "scene")][0], "scratch")
        self.assertEqual(m.lookup(0x7E18CE)[0][0].name, "record_y")   # slot 3
        self.assertFalse(m.lookup(0x7E0CF4, "scene"))

    def test_errors(self):
        page = '\n[[page]]\nname = "p"\nsubsystem = "core"\nmeaning = "m"\nsource = "s"\n%s\n'
        bad = {
            "no instances without d": page % 'why = "w"',
            "one named instance is a fixed page": page % 'why = "w"\ninstances = [{ name = "a", d = 0 }]',
            "instances with a fixed d": page % 'd = 0\ninstances = [{ name = "a", d = 0 }]',
            "one D twice": page % ('why = "w"\ninstances = [{ name = "a", d = 0x0B00 },'
                                   ' { name = "b", base = 0x0A00, count = 2, stride = 0x100 }]'),
            "an instance twice": page % 'why = "w"\ninstances = [{ name = "a", d = 0 }, { name = "a", d = 0x100 }]',
            "neither d nor an array": page % 'why = "w"\ninstances = [{ name = "a", base = 0 }]',
            "past work RAM": page % 'why = "w"\ninstances = [{ name = "a", base = 0x1E00, count = 3, stride = 0x100 }]',
            "a page kind's cells on a record's field":
                var("rec", 'addr = "7E:1800"\nstride = 0x40\ncount = 8\nindex = "slot"\n'
                           '[[var.field]]\nname = "y"\noffset = 0x0E\nmeaning = "m"', "menu")
                + var("record_y", 'page = "record"\ndp = 0x0E', "menu"),
        }
        for why, text in bad.items():
            with self.subTest(why=why), self.assertRaises(ram_map.MapError):
                self.load(text)

    def test_a_page_kind_may_name_a_records_unnamed_bytes(self):
        """A channel's page is its record: the record's fields and the page
        kind's cells split it between them."""
        self.load(var("channels", 'addr = "7E:0A00"\nstride = 0x100\ncount = 2\nindex = "channel"\n'
                                  '[[var.field]]\nname = "script"\noffset = 0x20\nsize = 2\nmeaning = "m"', "scene")
                  + var("scratch", 'page = "math"\ndp = 0xF0\nsize = 16', "scene"))


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
