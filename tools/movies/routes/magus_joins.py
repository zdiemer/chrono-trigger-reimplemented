"""Meet Magus at North Cape, refuse the fight, and recruit him."""
from ..campaign import run_route
from .north_cape import main


if __name__ == "__main__":
    raise SystemExit(run_route(lambda: main(join=True)))
