"""The native backends draw and play from their services and nothing else.

P3 of docs/PLATFORM.md: runtime/platform/native/ holds backends of the
services -- the renderer of the video service, the mixer of the voices -- so
each may read its service's header and the C library, and nothing in
runtime/snes/ or runtime/apu/: not the PPU register layer, not its VRAM, CGRAM
or OAM, not the scanline renderer it is checked against; not the S-DSP or its
registers. Everything a backend draws or plays has to reach it through what
its service describes, or a second backend (widescreen, GBA) would inherit the
hardware model with it.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE = ROOT / "runtime" / "platform" / "native"

# What a backend may include: its service, the platform conventions below it,
# and the C library.
ALLOWED_INCLUDES = {
    "platform/native/render.h",
    "platform/video.h",
    "platform/native/mixer.h",
    "platform/voices.h",
    "platform/voice_types.h",
    "platform/audio.h",
    "platform/platform.h",
    # the host backends of timing and input (host.h), whose frame runs the
    # game's main thread as the one coroutine fiber.h provides
    "platform/native/host.h",
    "platform/timing.h",
    "platform/input.h",
    "port/fiber.h",
    # the host's video backend keeps the picture's state in the observer, which
    # folds the video service's calls into a VidFrame and knows no registers
    "platform/observe.h",
    # and its saves are the save service's
    "platform/save.h",
}

# The hardware model's types and functions. `snes_extra*` is excluded: those are
# members of the video service's own structures, naming the bits of a SNES
# register that the hardware ignores.
FORBIDDEN_NAMES = ("Ppu", "Snes", "snes_(?!extra)", "ppu_", "render_line", "Renderer", "Dsp", "dsp_", "Apu", "apu_")


class TestNativeRenderer(unittest.TestCase):
    def sources(self):
        files = sorted(NATIVE.glob("*.c")) + sorted(NATIVE.glob("*.h"))
        self.assertTrue(files, "no sources in runtime/platform/native")
        return files

    def test_includes(self):
        for path in self.sources():
            for line in path.read_text(encoding="utf-8").splitlines():
                m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
                if m:
                    self.assertIn(m.group(1), ALLOWED_INCLUDES,
                                  f"{path.name} includes {m.group(1)}")
                m = re.match(r"\s*#\s*include\s+<([^>]+)>", line)
                if m:
                    self.assertTrue(m.group(1).endswith(".h"), f"{path.name}: {line}")

    def test_no_hardware_model_names(self):
        for path in self.sources():
            text = path.read_text(encoding="utf-8")
            # Comments may name the hardware model; code may not.
            code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            for name in FORBIDDEN_NAMES:
                self.assertNotRegex(code, r"\b" + name,
                                    f"{path.name} names {name} outside a comment")


if __name__ == "__main__":
    unittest.main()
