"""Carried fight routes' retries and facts (tools/movies/fight_facts.py); no ROM."""
import unittest
from unittest.mock import patch

from tools import outcomes
from tools.movies import fight_facts as F, route_check


def state(levels=(1,) * 7, gold=100, items=None, event=0):
    memory = bytearray(0x22000)
    for c, level in enumerate(levels):
        memory[0x2600 + c * 0x50 + 0x12] = level
        memory[0x2600 + c * 0x50 + 0x13] = 10 * level
    memory[0x2C53:0x2C56] = gold.to_bytes(3, "little")
    for i, (item, count) in enumerate((items or {}).items()):
        memory[0x2400 + i], memory[0x2500 + i] = item, count
    memory[0x10000] = event
    return bytes(memory)


class FightFactsTest(unittest.TestCase):
    def report(self, end, tries=(0,)):
        lines = []

        class Run:
            class machine:
                @staticmethod
                def peek(address, length):
                    return end[address - 0x7E0000:address - 0x7E0000 + length]

            @staticmethod
            def say(line):
                lines.append(line)
        F.report(Run, list(tries))
        return lines

    def expected(self, start, end):
        a, b = F.from_state(start), F.from_state(end)
        out = {k: b[k] for k in ("levels", "experience", "gold", "events")}
        out["items"] = F.items_text(F.kept_items(b["items"]))
        return out

    def test_a_run_that_ends_as_the_reference_passes(self):
        start = state(items={0x10: 1, 0xBD: 5})
        end = state(levels=(2,) * 7, gold=300, items={0x10: 1, 0x20: 2, 0xBD: 3}, event=5)
        expected = self.expected(start, end)
        self.assertEqual(expected["items"], "10:1,20:2")
        self.assertEqual(F.verdict("r", self.report(end), 0, expected), [])
        # a fight's tonics go with its course: not a fact
        used_more = state(levels=(2,) * 7, gold=300, items={0x10: 1, 0x20: 2, 0xBD: 1}, event=5)
        self.assertEqual(F.verdict("r", self.report(used_more), 0, expected), [])

    def test_lost_rewards_retries_and_story_fail(self):
        start, end = state(items={0x30: 1}), state(levels=(2,) * 7, gold=300, items={0x20: 2, 0x30: 1}, event=5)
        expected = self.expected(start, end)
        for run, tries, word in ((state(levels=(2,) * 7, gold=100, items={0x20: 2, 0x30: 1}, event=5), (0,), "gold"),
                                 (state(levels=(2,) * 7, gold=300, items={0x20: 1, 0x30: 1}, event=5), (0,), "item 20"),
                                 (state(levels=(2,) * 7, gold=300, items={0x20: 2}, event=5), (0,), "item 30"),
                                 (state(levels=(2,) * 7, gold=300, items={0x20: 2, 0x30: 1, 0x40: 1}, event=5), (0,),
                                  "item 40"),
                                 (state(levels=(1,) * 7, gold=300, items={0x20: 2, 0x30: 1}, event=5), (0,), "levels"),
                                 (state(levels=(2,) * 7, gold=300, items={0x20: 2, 0x30: 1}, event=6), (0,), "events"),
                                 (end, (0, 3), "retries")):
            with self.subTest(word=word):
                problems = F.verdict("r", self.report(run, tries), 2, expected)
                self.assertTrue(any(word in p for p in problems), problems)
        self.assertEqual(F.verdict("r", ["outcome reached x"], 0, expected), ["no fight report"])

    def test_every_carried_route_has_every_fact_and_only_they_do(self):
        expected = F.read_expected()
        carried = set(outcomes.fight_routes().values())
        self.assertEqual(set(expected), carried)
        for route, facts in expected.items():
            self.assertEqual(set(facts), {"levels", "experience", "gold", "items", "events"}, route)

    def test_only_carried_routes_are_held_to_their_facts(self):
        self.assertEqual(route_check.carried_fight("new_game_plus", []), [])
        self.assertEqual(route_check.carried_fight("son_of_sun", []), ["no fight report"])
        with patch.object(F, "read_expected", return_value={}):
            self.assertEqual(route_check.carried_fight("son_of_sun", []), ["no expected facts in tools/fight_facts.txt"])


if __name__ == "__main__":
    unittest.main()
