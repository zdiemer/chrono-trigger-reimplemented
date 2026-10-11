"""tools/vblank_check.py on made-up --io-log tables (no ROM needed)."""

import io
import pathlib
import tempfile
import unittest

from tools import vblank_check as vc

HEADER = ("# ct_dev --io-log: hardware register accesses by origin\n"
          "# kind origin site service register group access count first-frame"
          " entry vblank blank visible picture-lines first-picture-frame visible-lines unblank-lines\n")


def row(kind, origin, site, reg, vblank=0, blank=0, visible=0, entry="-", service="-",
        visible_lines="-", unblank_lines="-", group="ppu"):
    count = vblank + blank + visible
    lines = "0-224" if blank or visible else "-"
    return "%s %s %s %s $%04X %s write %d 0 %s %d %d %d %s %s %s %s\n" % (
        kind, origin, site, service, reg, group, count, entry, vblank, blank, visible, lines,
        "0" if lines != "-" else "-", visible_lines, unblank_lines)


class VblankCheckTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def log(self, name, rows):
        path = pathlib.Path(self.dir.name) / name
        path.write_text(HEADER + "".join(rows))
        return path

    def findings(self, ref_rows, ship_rows, blocks=()):
        ref = self.log("ref.txt", ref_rows)
        ship = self.log("ship.txt", ship_rows)
        routines = vc.Routines([ship], blocks)
        return routines, vc.compare(vc.read_log(ref), vc.read_log(ship), routines)

    def test_port_matched_to_generated_code_in_its_region(self):
        # The reference's block at $C0:1010 lies between the port's entry and its
        # last call site, so it is the same routine.
        routines, found = self.findings(
            [row("generated", "generated_C01010_m8x16", "C0:1010", 0x2100, vblank=50)],
            [row("port-service", "demo_nmi", "C0:1000", 0x4200, vblank=50, entry="C0:1000",
                 service="tm_enable_interrupts", group="cpu"),
             row("port-service", "demo_nmi", "C0:1020", 0x2100, vblank=40, blank=10, entry="C0:1000",
                 service="vid_set_display", unblank_lines="1:4,30:6")])
        self.assertEqual(routines.of_code(0xC01010), ("port", 0xC01000))
        got = {(f.category, f.group): f.count for f in found}
        # Ending forced blank before line 1 is drawn costs nothing; on line 30 it does.
        self.assertEqual(got, {("unblank", "display"): 6, ("late", "display"): 10})

    def test_visible_writes(self):
        _, found = self.findings(
            [row("generated", "generated_C02000_m8x16", "C0:2000", 0x2118, vblank=100, group="dma-data"),
             row("generated", "generated_C02000_m8x16", "C0:2000", 0x2105, vblank=5)],
            [row("generated", "generated_C02000_m8x16", "C0:2000", 0x2118, vblank=90, visible=10, group="dma-data",
                 visible_lines="0:10"),
             row("generated", "generated_C02000_m8x16", "C0:2000", 0x2105, vblank=3, visible=2,
                 visible_lines="1:2")])
        # Lost VRAM data counts on any line; a mode change seen by every shown line doesn't.
        self.assertEqual([(f.category, f.group, f.count) for f in found], [("visible", "vram", 10)])

    def test_raster_writes_that_moved(self):
        _, found = self.findings(
            [row("generated", "generated_C03000_m8x16", "C0:3000", 0x2100, visible=100, visible_lines="212:100")],
            [row("generated", "generated_C03000_m8x16", "C0:3000", 0x2100, visible=100,
                 visible_lines="212:97,211:3")])
        self.assertEqual([(f.category, f.count, dict(f.lines)) for f in found], [("lines", 3, {211: 3})])

    def test_unmatched_and_exceptions(self):
        routines, found = self.findings(
            [],
            [row("port-service", "demo_irq", "C0:4000", 0x2100, visible=5, entry="C0:4000",
                 visible_lines="100:5")])
        self.assertEqual([f.category for f in found], ["unmatched"])
        out = io.StringIO()
        self.assertEqual(vc.report([("m", found)], routines, set(), out=out), 0)

        _, found = self.findings(
            [row("generated", "generated_C04000_m8x16", "C0:4000", 0x2100, vblank=5)],
            [row("port-service", "demo_irq", "C0:4000", 0x2100, visible=5, entry="C0:4000",
                 visible_lines="100:5")])
        self.assertEqual(vc.report([("m", found)], routines, set(), out=io.StringIO()), 1)
        known = {("demo_irq", "display", "visible")}
        self.assertEqual(vc.report([("m", found)], routines, known, out=io.StringIO()), 0)
        self.assertEqual(vc.report([("m", found)], routines, {("$C0:4000", "*", "visible")}, out=io.StringIO()), 0)

    def test_origin_names_with_spaces(self):
        path = self.log("ship.txt", [row("generated", "(resumed generated block)", "C0:5004", 0x2100, vblank=1)])
        rows = vc.read_log(path)
        self.assertEqual((rows[0].origin, rows[0].site), ("(resumed generated block)", 0xC05004))
        self.assertEqual(vc.Routines([], [0xC05000]).of_row(rows[0]), ("block", 0xC05000))

    def test_exceptions_file_is_well_formed(self):
        vc.read_exceptions(vc.EXCEPTIONS)


if __name__ == "__main__":
    unittest.main()
