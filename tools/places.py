"""Native place sequences compared with recorded targets."""
import re

PLACE = re.compile(r"^location: frame (\d+) \$([0-9A-Fa-f]+)", re.M)
# A place entered this near the reference's frame is entered on time: half a
# second, about what a load or a fade the time model gets wrong by moves it.
ON_TIME = 30


def parse(text):
    """[(frame, place)] in the order entered."""
    if isinstance(text, bytes):
        text = text.decode(errors="replace")
    return [(int(f), int(p, 16)) for f, p in PLACE.findall(text)]


class Kept:
    """How a run's places keep to the reference's: the places it enters in the
    reference's order, up to the first that is another place (or none); of
    those, the ones entered on time; and the latest and earliest of them."""

    def __init__(self, reference, run, on_time=ON_TIME):
        self.total = len(reference)
        self.same = 0           # places in the reference's order
        self.kept = 0           # ... of them on time
        self.worst = 0          # the run's frame minus the reference's, the largest either way
        self.parted = None      # (reference frame, reference place, run's (frame, place) or None)
        for i, (frame, place) in enumerate(reference):
            theirs = run[i] if i < len(run) else None
            if theirs is None or theirs[1] != place:
                self.parted = (frame, place, theirs)
                break
            self.same += 1
            off = theirs[0] - frame
            self.kept += abs(off) <= on_time
            if abs(off) > abs(self.worst):
                self.worst = off

    def summary(self):
        text = "%d of %d places on time, %d in the reference's order" % (self.kept, self.total, self.same)
        if self.worst:
            text += " (at worst %+d frames)" % self.worst
        if self.parted:
            frame, place, theirs = self.parted
            if theirs is None:
                text += "; no $%03X at %d" % (place, frame)
            else:
                text += "; $%03X at %d where the reference enters $%03X at %d" % (theirs[1], theirs[0], place, frame)
        return text
