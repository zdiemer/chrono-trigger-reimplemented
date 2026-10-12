"""tools/generate/groundtruth.py's reading of a port, on a made-up port (no ROM needed).

    python3 -m unittest discover tests

A port in the style the $C7 pilot settled on names work RAM through its own
macros over game/ram.h's names and calls out through helpers that take the
site and target as parameters; the ground truth has to see through both to
count the port's accesses and call-outs.
"""

import pathlib
import tempfile
import unittest

from tools.generate import groundtruth as gt

RAM_H = """
#define RAM_ABS(addr) ((uint16_t)((addr) & 0xFFFFu))
#define RAM_SOUND_COUNTER 0x7E1E84u
#define RAM_SOUND_SLOTS 0x7E1E90u
"""

PORT = """
#include "ram.h"

#define COUNTER RAM_ABS(RAM_SOUND_COUNTER)
#define SLOTS RAM_ABS(RAM_SOUND_SLOTS)

static uint8_t slot_byte(Snes *s, uint16_t table, uint16_t at) { return m_read8(s, (uint16_t)(table + at)); }

static bool call_driver(Snes *s, uint16_t site, uint16_t target)
{
    au_driver_call(m_platform(s), PF_CALL(0xC70000u | site, 0xC70000u | target), DC_X);
    return true;
}

static bool send_counter(Snes *s, uint16_t site) { return call_driver(s, site, 0x09DA); }

// $C7:0100
void port(Snes *s)
{
    m_write8(s, COUNTER, slot_byte(s, SLOTS, 2));
    send_counter(s, 0x0105);
}

// $C7:0200
void other(Snes *s) { call_driver(s, 0x0203, 0x0655); }
"""


class GroundTruth(unittest.TestCase):
    def test_file_local_names_and_call_out_helpers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "game" / "core").mkdir(parents=True)
            (root / "game" / "ram.h").write_text(RAM_H, encoding="utf-8")
            (root / "game" / "core" / "port.c").write_text(PORT, encoding="utf-8")
            path, name, texts, consts, funcs = gt.find_port(0xC70100, root / "game")
            self.assertEqual(name, "port")
            resolver = gt.Resolver(path, root)
            text = gt.strip("\n".join(texts))
            addrs, named, numbered = gt.port_addresses(text, consts, {}, None, None, resolver)
            self.assertEqual(addrs, {0x7E1E84, 0x7E1E90})
            self.assertEqual((named, numbered), (2, 0))
            # the helper's target, from the calls the port makes only
            self.assertEqual(gt.port_callouts(text, consts, resolver, funcs), {0xC709DA})


if __name__ == "__main__":
    unittest.main()
