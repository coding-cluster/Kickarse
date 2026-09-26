"""Capture the real NanoVG UI of the built VST2 plugin (debug snapshot hook).

The standalone Kickarse.exe needs an audio device with 4 inputs, so snapshots use a tiny VST2
editor host (tools/assets/uihost, built to LOCALAPPDATA/KickarseBuild/uihost). For every state
the host is started with
    KICKARSE_UI_SHOT=<state> KICKARSE_UI_SCALE=<s> KICKARSE_UI_SNAPSHOT=<bmp> KICKARSE_UI_SNAPSHOT_EXIT=1
renders ~20 frames with synthetic Bridge data, writes a BMP and exits by itself. The BMP is
converted to PNG in design/prototype/screenshots/native/. A process that does not exit in time is
terminated by the PID this script started (never by name).

Usage: python tools/assets/ui_snapshots.py [--exe PATH] [state ...]
"""
from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "design" / "prototype" / "screenshots" / "native"
BUILD = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "KickarseBuild"
DEFAULT_EXE = BUILD / "ui" / "bin" / "Kickarse-vst2.dll"
HOST = BUILD / "uihost" / "Release" / "kickarse_uihost.exe"
STATES = ["sync", "midi", "audio", "spectral", "ring", "multi", "depth60", "record", "oneshot", "presets", "rate",
          "menu", "entry", "ms"]
STATES_2X = ["sync", "spectral", "multi", "presets"]


def shoot(exe: pathlib.Path, state: str, scale: float) -> pathlib.Path | None:
    OUT.mkdir(parents=True, exist_ok=True)
    bmp = pathlib.Path(tempfile.gettempdir()) / f"kickarse_{state}_{scale:g}.bmp"
    if bmp.exists():
        bmp.unlink()
    env = dict(os.environ, KICKARSE_UI_SHOT=state, KICKARSE_UI_SCALE=str(scale), KICKARSE_UI_SNAPSHOT=str(bmp),
               KICKARSE_UI_SNAPSHOT_EXIT="1")
    proc = subprocess.Popen([str(HOST), str(exe), "25"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()          # only the process this script started
        proc.wait()
    if not bmp.exists():
        print(f"{state}@{scale:g}x: no snapshot")
        return None
    out = OUT / f"{state}@{scale:g}x.png"
    Image.open(bmp).save(out, optimize=True)
    bmp.unlink()
    print(out.relative_to(ROOT))
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    ap.add_argument("states", nargs="*")
    a = ap.parse_args()
    for s in a.states or STATES:
        shoot(a.exe, s, 1.0)
    if not a.states:
        for s in STATES_2X:
            shoot(a.exe, s, 2.0)


if __name__ == "__main__":
    main()
