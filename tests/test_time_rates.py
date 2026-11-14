"""The work rates stay within what the time model keeps them in.

runtime/port/state_host.c keeps the rates the scopes named lately with each
split into whole clocks and thousandths (`RecentRow`), the whole clocks in 16
bits. The table is frozen (runtime/port/time_rates.c); this holds any change
to it to the bound.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATES = ROOT / "runtime" / "port" / "time_rates.c"
ROW = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*(\d+)u?\s*\}")
MOST_WHOLE_CLOCKS = 0xFFFF


class TestTimeRates(unittest.TestCase):
    def test_every_rate_fits_the_recent_rows(self):
        rows = ROW.findall(RATES.read_text(encoding="utf-8"))
        self.assertTrue(rows, "no rates found")
        over = [(r, int(c)) for r, c in rows if int(c) // 1000 > MOST_WHOLE_CLOCKS]
        self.assertEqual(over, [], "rates over %d whole clocks" % MOST_WHOLE_CLOCKS)


if __name__ == "__main__":
    unittest.main()
