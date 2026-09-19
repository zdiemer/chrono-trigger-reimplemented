"""tools/timing_gate.py on made-up --timing-report output (no ROM needed)."""

import pathlib
import re
import tempfile
import unittest

from tools import timing_gate as tg


def hist(**buckets):
    """A 24-bucket error histogram: hist(b0=5, b11=2) is 5 exact, 2 of 1024-2047 clocks."""
    out = [0] * 24
    for k, n in buckets.items():
        out[int(k[1:])] = n
    return out


def entry(name, stretches, original, bus, error, cost_milli, errors, max_error, addr="C01234", dma=0,
          vblank=0, vblank_errors=None, vblank_max=0, picture_max=None):
    """The lines ct_dev --timing-report prints for one hand-port entry."""
    if vblank_errors is None:
        vblank_errors = hist()
    if picture_max is None:
        picture_max = max_error
    return ("  %s %-40s calls %d  matched %d  mismatched 0  interrupted 0  original/native-bus 1.000\n"
            "    timing: stretches %d  estimate exact %d  mean |error| %.1f  max |error| %d  of %.1f"
            "  calibrated cost_milli %.0f\n"
            "    timing: totals original %d native-bus %d dma %d |error| %d cost_milli %d  error bits %s\n"
            "    timing: vblank stretches %d  max |error| %d  picture max |error| %d  vblank error bits %s\n"
            "    timing: fit horizon 0 (0.0%% of time)  fit interrupts 0 (0.0%% of time)\n"
            "    timing: unseen time values 1: 0\n"
            % (addr, name, stretches, stretches, stretches, errors[0], error / stretches, max_error,
               original / stretches, 1000.0 * original / bus, original, bus, dma, error, cost_milli,
               " ".join(str(n) for n in errors), vblank, vblank_max, picture_max,
               " ".join(str(n) for n in vblank_errors)))


REPORT = "native routines (shadow mode):\n"


def parse(*entries):
    return tg.parse_report(REPORT + "".join(entries))


class TimingGateTest(unittest.TestCase):
    def test_parse_and_aggregate_weights_by_stretches(self):
        a = parse(entry("port_a", 100, 400000, 100000, 1000, 4000, hist(b0=90, b5=10), 20,
                        vblank=10, vblank_errors=hist(b5=10), vblank_max=20, picture_max=0))
        b = parse(entry("port_a", 300, 1320000, 300000, 3000, 4000, hist(b0=300), 0))
        self.assertEqual(a["port_a"].addr, 0xC01234)
        e = tg.aggregate([a, b])["port_a"]
        self.assertEqual((e.stretches, e.original, e.bus, e.error, e.max_error, e.movies),
                         (400, 1720000, 400000, 4000, 20, 2))
        self.assertEqual((e.vblank_stretches, e.vblank_max, e.picture_max), (10, 20, 0))
        self.assertEqual(e.hist[0], 390)
        self.assertEqual(e.vblank_hist[5], 10)
        self.assertEqual(e.calibrated, 4300)
        self.assertAlmostEqual(e.drift, 1600000 / 1720000 - 1)

    def test_mixed_builds_are_refused(self):
        a = parse(entry("p", 100, 1000, 250, 0, 4000, hist(b0=100), 0))
        b = parse(entry("p", 100, 1000, 250, 0, 3000, hist(b0=100), 0))
        with self.assertRaises(ValueError):
            tg.aggregate([a, b])

    def test_dma_time_is_left_out_of_the_estimate(self):
        # 1000 clocks of DMA in 3000 bus clocks; the other 2000 took 8000 in the original.
        e = parse(entry("dma_port", 100, 9000, 3000, 0, 4000, hist(b0=100), 0, dma=1000))["dma_port"]
        self.assertEqual(e.calibrated, 4000)
        self.assertEqual(e.estimate, 9000)
        self.assertAlmostEqual(e.drift, 0.0)

    def test_limits(self):
        agg = tg.aggregate([parse(
            entry("good", 1000, 4000000, 1000000, 5000, 4000, hist(b0=500, b3=500), 7,
                  vblank=500, vblank_errors=hist(b0=250, b3=250), vblank_max=7),
            entry("drift", 1000, 4100000, 1000000, 100000, 4000, hist(b7=1000), 100),
            # off by a scanline often, but only on picture lines: not a vblank-tail failure
            entry("picture_tail", 1000, 4000000, 1000000, 90000, 4000, hist(b0=900, b11=100), 2000),
            entry("vblank_tail", 1000, 4000000, 1000000, 90000, 4000, hist(b0=900, b11=100), 2000,
                  vblank=200, vblank_errors=hist(b0=100, b11=100), vblank_max=2000, picture_max=0),
            entry("vblank_worst", 10, 40000, 10000, 20000, 4000, hist(b0=9, b15=1), 20000,
                  vblank=1, vblank_errors=hist(b15=1), vblank_max=20000, picture_max=0),
            entry("picture_worst", 10, 400000, 100000, 60000, 4000, hist(b0=9, b16=1), 60000),
            entry("picture_spike", 10, 400000, 100000, 20000, 4000, hist(b0=9, b15=1), 20000),
            entry("few", 10, 50000, 10000, 10000, 4000, hist(b11=10), 1100,
                  vblank=10, vblank_errors=hist(b11=10), vblank_max=1100),
        )])
        self.assertEqual(tg.exceeded(agg["good"]), {})
        self.assertEqual(set(tg.exceeded(agg["drift"])), {"drift"})
        self.assertEqual(tg.exceeded(agg["picture_tail"]), {})
        self.assertEqual(set(tg.exceeded(agg["vblank_tail"])), {"vblank-tail"})
        self.assertEqual(set(tg.exceeded(agg["vblank_worst"])), {"vblank-worst"})
        self.assertEqual(set(tg.exceeded(agg["picture_worst"])), {"worst"})
        self.assertEqual(tg.exceeded(agg["picture_spike"]), {})   # under a vblank's length on a picture line
        self.assertEqual(tg.exceeded(agg["few"]), {})              # too few stretches to judge drift or tail

    def test_exceptions_known_and_stale(self):
        agg = tg.aggregate([parse(entry("drift", 1000, 4100000, 1000000, 100000, 4000, hist(b7=1000), 100),
                                  entry("good", 1000, 4000000, 1000000, 0, 4000, hist(b0=1000), 0))])
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "exceptions.txt"
            path.write_text("# comment\ndrift drift the reason\ngood worst was bad once\n")
            result = tg.check(agg, tg.read_exceptions(path))
            self.assertEqual(result.failures, [])
            self.assertEqual([(e.name, lim) for e, lim, _v, _b in result.known], [("drift", "drift")])
            self.assertEqual(result.stale, [("good", "worst")])
            self.assertIn("1 known", tg.summary(result))
            self.assertIn("good is listed for worst", tg.failure_lines(result)[0])
            result = tg.check(agg, {})
            self.assertEqual([(e.name, lim) for e, lim, _v, _b in result.failures], [("drift", "drift")])
            self.assertIn("1 of 2 entries", tg.summary(result))
            self.assertIn("drift over the drift limit: -2.44% (limit +/-0.38%)", tg.failure_lines(result)[0])
            path.write_text("drift speed no such limit\n")
            with self.assertRaises(SystemExit):
                tg.read_exceptions(path)

    def test_calibrate_rewrites_registry(self):
        agg = tg.aggregate([parse(entry("a_port", 1000, 4321000, 1000000, 0, 4000, hist(b0=1000), 0))])
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "registry.c"
            path.write_text('const NativeEntry t[] = {\n'
                            '    { .addr = 0xC01234, .name = "a_port", .fn = a_port, NEEDS_M8,\n'
                            '      .cost_milli = 4000, .dont_care = DC_N },\n'
                            '    { .addr = 0xC05678, .name = "other", .fn = other, .cost_milli = 3000 },\n'
                            '};\n')
            self.assertEqual(tg.calibrate(agg, path), [("a_port", 4000, 4321)])
            text = path.read_text()
            self.assertIn(".cost_milli = 4321, .dont_care", text)
            self.assertIn(".cost_milli = 3000 }", text)

    def test_calibrate_rewrites_table_macros(self):
        agg = tg.aggregate([parse(entry("event_cmd_x", 1000, 5500000, 1000000, 0, 5000, hist(b0=1000), 0))])
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "registry.c"
            path.write_text("    EVENT_CMD(0xC062B5, event_cmd_x, 5000),\n    EVENT_CMD(0xC062CB, event_cmd_y, 5000),\n")
            self.assertEqual(tg.calibrate(agg, path), [("event_cmd_x", 5000, 5500)])
            self.assertEqual(path.read_text(), "    EVENT_CMD(0xC062B5, event_cmd_x, 5500),\n"
                                               "    EVENT_CMD(0xC062CB, event_cmd_y, 5000),\n")
            missing = tg.aggregate([parse(entry("gone", 10, 100, 10, 0, 4000, hist(b0=10), 0))])
            with self.assertRaises(SystemExit):
                tg.calibrate(missing, path)

    def test_parts_are_estimated_at_their_routines_cost(self):
        # A dispatcher: 1000 bus clocks of its own (at 4000), and 3000 it ran
        # inline for a handler (at 2000, 500 of them DMA): 4000 + 500 + 5000.
        report = (entry("dispatcher", 100, 9500, 4000, 0, 4000, hist(b0=100), 0, dma=500)
                  + part("C0AAAA", "handler", 3000, 500, 2000)
                  + entry("handler", 100, 4000, 2000, 0, 2000, hist(b0=100), 0, addr="C0AAAA"))
        agg = tg.aggregate([tg.parse_report(REPORT + report), tg.parse_report(REPORT + report)])
        e = agg["dispatcher"]
        self.assertEqual((e.own_bus, e.own_dma), (2000, 0))
        self.assertEqual(e.parts["handler"].bus, 6000)
        self.assertEqual(e.estimate, 2 * 9500)
        self.assertAlmostEqual(e.drift, 0.0)
        self.assertEqual(e.calibrated, 4000)
        # The handler calibrates to 3000 on its own calls; the dispatcher's
        # own part then takes what the handler's new estimate leaves.
        agg["handler"].original = 2 * 6000
        values = tg.calibrated_values(agg)
        self.assertEqual(values["handler"], 3000)
        self.assertEqual(values["dispatcher"], round(1000.0 * (19000 - 1000 - 2 * 2500 * 3) / 2000))

    def test_misattribution_fails_without_exception(self):
        agg = tg.aggregate([tg.parse_report(
            REPORT + entry("port", 100, 400, 100, 0, 4000, hist(b0=100), 0)
            + "    timing: attributions to no hand port 7 (last C01234)  past the parts' room 0\n")])
        result = tg.check(agg, {("port", "drift"): "no"})
        self.assertEqual([(f[0].name, f[1]) for f in result.failures], [("port", "attribution")])
        self.assertIn("port attributed time to $C0:1234 7 time(s)", tg.failure_lines(result)[0])

    def test_time_tags_in_game_name_hand_ports(self):
        """Every literal address a port attributes time to (native_time_as,
        NATIVE_TIME_AS) has a hand port registered there; shadow mode reports
        the rest (computed ones) as it meets them."""
        root = pathlib.Path(__file__).resolve().parents[1]
        registry = (root / "game/registry.c").read_text()
        registered = {int(a, 16) for a in re.findall(r"\b0x([0-9A-Fa-f]{6})\b", registry)}
        tags = []
        for path in sorted((root / "game").rglob("*.c")):
            for m in re.finditer(r"(?:native_time_as|NATIVE_TIME_AS)\(\s*\w+\s*,\s*0x([0-9A-Fa-f]{6})\s*[,)]",
                                 path.read_text()):
                tags.append((path.name, int(m.group(1), 16)))
        self.assertTrue(tags)
        self.assertEqual([(f, "%06X" % a) for f, a in tags if a not in registered], [])


def part(addr, name, bus, dma, cost_milli):
    return "    timing: part %s %s native-bus %d dma %d cost_milli %d\n" % (addr, name, bus, dma, cost_milli)


if __name__ == "__main__":
    unittest.main()
