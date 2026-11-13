"""The gate's shared ct_native slots: at most so many ct_native processes at once
across every tool the gate runs side by side (tools/suite.py).

The suite names a directory and a count in the environment (CT_NATIVE_SLOTS,
`dir:count`); each tool holds a slot, a locked file in that directory, for as
long as a ct_native process of its own runs. Without the variable a slot is
nothing, and a tool alone keeps to its own -j.
"""
import contextlib
import os
import time

if os.name == "nt":
    import msvcrt
else:
    import fcntl

ENV = "CT_NATIVE_SLOTS"
POLL = 0.05


def _try_lock(f):
    try:
        f.seek(0)
        if os.name == "nt":
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def _unlock(f):
    if os.name == "nt":
        f.seek(0)
        msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
    else:
        fcntl.flock(f.fileno(), fcntl.LOCK_UN)


@contextlib.contextmanager
def slot():
    """Hold one of the gate's ct_native slots for the block (none outside the gate)."""
    spec = os.environ.get(ENV)
    if not spec:
        yield
        return
    where, _, count = spec.rpartition(":")
    os.makedirs(where, exist_ok=True)
    while True:
        for n in range(int(count)):
            f = open(os.path.join(where, "slot-%d" % n), "a+b")
            if _try_lock(f):
                try:
                    yield
                finally:
                    _unlock(f)
                    f.close()
                return
            f.close()
        time.sleep(POLL)
