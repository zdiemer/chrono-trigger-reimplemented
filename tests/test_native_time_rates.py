"""The native model keeps rates when their entries leave the registry."""
from pathlib import Path
import unittest

from tools import time_rates


class NativeTimeRatesTest(unittest.TestCase):
    def test_retired_macro_rows_are_complete(self):
        source = (Path(__file__).resolve().parents[1] / "runtime/port/retired_time_rates.h").read_text()
        time_rates.check_complete(source, "retired rates")
        self.assertEqual(time_rates.table([source]), [(0xC30002, 4060), (0xC30557, 4057)])

    def test_sources_keep_first_rate_and_sort_by_address(self):
        registered = "{ .addr = 0xC30002, .cost_milli = 4000 },"
        retired = "    ENTRY(0xC30557, direct, 4057)\n    ENTRY(0xC30002, unpack, 4060)\n"
        self.assertEqual(time_rates.table([registered, retired]), [(0xC30002, 4000), (0xC30557, 4057)])

    def test_field_rates_survive_the_battle_callers_retirement(self):
        root = Path(__file__).resolve().parents[1]
        rates = dict(time_rates.table([(root / "game/registry.c").read_text(),
                                      (root / "game/time_tags.c").read_text()]))
        self.assertEqual({address: rates.get(address) for address in (0xC00005, 0xC00008, 0xC0000B)},
                         {0xC00005: 4307, 0xC00008: 5024, 0xC0000B: 5056})

    def test_unparsed_rate_fails_instead_of_silently_losing_time(self):
        with self.assertRaises(SystemExit):
            time_rates.check_complete("    ENTRY(0xC30002, unpack, RATE)\n", "bad rate")


if __name__ == "__main__":
    unittest.main()
