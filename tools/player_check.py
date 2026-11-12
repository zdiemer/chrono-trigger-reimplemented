#!/usr/bin/env python3
"""Check native checkpoints and recording with a local ROM, one process at a time.

    python3 tools/player_check.py --build build/main --rom roms/ct-usa.sfc

Outputs are ROM-derived and go only to the ignored build directory. The
ROM-free format/bindings/name checks are separate CMake test executables.
"""
import argparse
import os
from pathlib import Path
import subprocess
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build/main"))
    parser.add_argument("--rom", type=Path, default=Path("roms/ct-usa.sfc"))
    parser.add_argument("--out", type=Path, default=Path("build/player-check"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, rom, out = args.build.resolve(), args.rom.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""

    def run(app, label, *options, input_text=None):
        with (out / (label + ".log")).open("wb") as log:
            subprocess.run([str(build / (app + suffix)), "--rom", str(rom), *map(str, options)],
                           cwd=root, input=input_text.encode() if input_text is not None else None,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)

    def pcm(label):
        with wave.open(str(out / (label + ".wav")), "rb") as stream:
            return stream.readframes(stream.getnframes())

    for name, movie, cut, end in (
        ("opening", "new-game", 5200, 7200),
        ("menu", "menu", 7400, 8000),
        ("battle", "saves/battle-magus", 5000, 5600),
    ):
        source = root / "oracle/movies" / (movie + ".ctm")
        state = out / (name + ".native-state")
        run("ct_native", name + "-prefix", "--movie", source, "--frames", cut,
            "--save-state", state, "--wav", out / (name + "-prefix.wav"))
        for kind in ("whole", "resumed"):
            options = ["--load-state", state] if kind == "resumed" else []
            run("ct_native", name + "-" + kind, "--movie", source, "--frames", end,
                "--end-state", out / (name + "-" + kind + ".bin"),
                "--screenshot", out / (name + "-" + kind + ".png"),
                "--wav", out / (name + "-" + kind + ".wav"), *options)
        for extension in ("bin", "png"):
            if (out / f"{name}-whole.{extension}").read_bytes() != (out / f"{name}-resumed.{extension}").read_bytes():
                raise RuntimeError(f"{name}: continued {extension} differs")
        if pcm(name + "-whole")[len(pcm(name + "-prefix")):] != pcm(name + "-resumed"):
            raise RuntimeError(f"{name}: continued audio differs")
        print(f"{name}: continued state, picture and audio identical", flush=True)

    # Route slots reuse exact audio-host replies; file loads reconstruct sound
    # normally. Both go through the player's incremental checkpoint engine.
    route_state = out / "route.state"
    commands = [f"movie {root / 'oracle/movies/menu.ctm'} 7400", "save 0", f"savefile {route_state}"]
    for kind, restore in (("whole", None), ("slot", "load 0"), ("file", f"loadfile {route_state}")):
        if restore:
            commands.append(restore)
        commands += ["run 300 0", f"wram {out / ('route-' + kind + '.bin')}",
                     f"shot {out / ('route-' + kind + '.png')}"]
    commands.append("quit")
    run("ct_native", "route", "--server", input_text="\n".join(commands) + "\n")
    if any(line.startswith("err ") for line in (out / "route.log").read_text().splitlines()):
        raise RuntimeError("route checkpoint command failed")
    for extension in ("bin", "png"):
        expected = (out / f"route-whole.{extension}").read_bytes()
        for kind in ("slot", "file"):
            if (out / f"route-{kind}.{extension}").read_bytes() != expected:
                raise RuntimeError(f"route {kind}: continued {extension} differs")
    print("route: slot/audio and file restores preserve continued state and picture", flush=True)

    source = root / "oracle/movies/new-game.ctm"
    recording = out / "recorded.ctm"
    run("ct_native", "record", "--movie", source, "--record", recording, "--frames", 7200,
        "--end-state", out / "record.bin")
    run("ct_native", "replay", "--movie", recording, "--frames", 7200, "--end-state", out / "replay.bin")
    if (out / "record.bin").read_bytes() != (out / "replay.bin").read_bytes():
        raise RuntimeError("recording changed native playback")
    run("ct_play", "play", "--movie", recording, "--frames", 7200)
    for name, movie in (("source", source), ("recorded", recording)):
        run("ct_dev", name, "--movie", movie, "--frames", 7200, "--native", "off",
            "--trace", out / (name + ".cttr"))
    if (out / "source.cttr").read_bytes() != (out / "recorded.cttr").read_bytes():
        raise RuntimeError("recording changed reference playback")
    print("recording: native state and reference traces identical; ct_play accepted it", flush=True)
    print("player check OK")


if __name__ == "__main__":
    main()
