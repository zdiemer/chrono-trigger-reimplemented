"""An Epoch landing approach must survive a different input-sampling phase."""
import unittest

from tools.movies.routes.son_of_sun import fly_to


class Flight:
    """Tile movement finishes after release; input can arrive a frame late."""

    def __init__(self, delay, blocked=False):
        self.machine = self
        self.position = [280, 224]
        self.goal = self.position.copy()
        self.pending = [0] * delay
        self.blocked = blocked

    def world_pos(self):
        return tuple(self.position)

    def run(self, frames, direction=0):
        directions = {"RIGHT": (0, 8), "LEFT": (0, -8),
                      "DOWN": (1, 8), "UP": (1, -8)}
        for _ in range(frames):
            self.pending.append(direction)
            held = self.pending.pop(0)
            if self.position == self.goal and held and not self.blocked:
                axis, step = directions[held]
                self.goal[axis] += step
            for axis in range(2):
                distance = self.goal[axis] - self.position[axis]
                self.position[axis] += max(-2, min(2, distance))


class FlightApproachTest(unittest.TestCase):
    def test_settled_target_with_immediate_or_delayed_input(self):
        for delay in (0, 1, 2):
            with self.subTest(delay=delay):
                flight = Flight(delay)
                for destination in ((544, 936), (528, 912), (280, 224)):
                    fly_to(flight, *destination)
                    self.assertEqual(flight.world_pos(), destination)
                    flight.run(24)
                    self.assertEqual(flight.world_pos(), destination)

    def test_blocked_approach_fails_instead_of_claiming_a_landing(self):
        with self.assertRaisesRegex(AssertionError, "settles"):
            fly_to(Flight(0, blocked=True), 288, 224)
