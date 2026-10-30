"""The gate's sample of the movie library (tools/gate_movies.txt, read by
tools/suite.py): every line names a movie in the library and says why it is
there, so the default gate can't silently run fewer movies than it lists.
Needs no ROM.
"""
import unittest

from tools import suite


class GateMoviesTest(unittest.TestCase):
    def test_every_line_names_a_library_movie_with_its_reason(self):
        names, bad = suite.gate_movies()
        self.assertEqual(bad, [])
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)), "a movie listed twice")

    def test_the_sample_is_part_of_the_library(self):
        names, _ = suite.gate_movies()
        library = suite.collect("all", set())
        self.assertLess(len(names), len(library))
        self.assertEqual(sorted(m[0] for m in suite.collect("all", set(names))), sorted(names))

    def test_the_trace_identity_movie_is_sampled(self):
        """The trace check plays new-game itself, but the sample keeps it so the
        shadow runs cover the same play."""
        names, _ = suite.gate_movies()
        self.assertIn("new-game", names)


if __name__ == "__main__":
    unittest.main()
