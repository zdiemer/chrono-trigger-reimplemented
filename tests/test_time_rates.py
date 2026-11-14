"""The work rates stay within what the time model's 32-bit sums can carry.

runtime/port/state_host.h charges an access inline, in 32 bits: its clocks
times a rate's whole clocks and its thousandths. That can't overflow while
every rate is at most 4,096 whole clocks (STATE_HOST_ONE_ACCESS_MOST clocks
at a time, under STATE_HOST_CLOCKS_NEAR_FULL). The table is frozen
(runtime/port/time_rates.c); this holds any change to it to the bound.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
RATES = ROOT / "runtime" / "port" / "time_rates.c"
ROW = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*(\d+)u?\s*\}")
MOST_WHOLE_CLOCKS = 4096


class TestTimeRates(unittest.TestCase):
    def test_every_rate_fits_the_inline_charge(self):
        rows = ROW.findall(RATES.read_text(encoding="utf-8"))
        self.assertTrue(rows, "no rates found")
        over = [(r, int(c)) for r, c in rows if int(c) // 1000 > MOST_WHOLE_CLOCKS]
        self.assertEqual(over, [], "rates over %d whole clocks" % MOST_WHOLE_CLOCKS)


if __name__ == "__main__":
    unittest.main()
