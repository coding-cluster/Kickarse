"""Render deterministic screenshots of the HTML prototype with headless Chrome/Edge.

Each shot loads design/prototype/index.html?shot=<state>&scale=<s>&freeze=<phase>; the prototype
hides its dev bar, pins the playhead and pre-fills the waveform history, so repeated runs give the
same picture (apart from the simulated hi-hat noise, which is seeded).

Usage:
    python tools/assets/screenshots.py                # all shots at 1x and a few at 2x
    python tools/assets/screenshots.py sync audio     # only these states (1x)
Output: design/prototype/screenshots/<state>@<scale>x.png
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAGE = ROOT / "design" / "prototype" / "index.html"
OUT = ROOT / "design" / "prototype" / "screenshots"
W, H = 1080, 660

BROWSERS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
]

SHOTS = ["sync", "midi", "audio", "spectral", "ring", "multi", "edit", "depth60", "record", "oneshot", "presets", "rate", "entry", "menu", "hover", "ms"]
SHOTS_2X = ["sync", "spectral", "multi", "edit"]
# playhead position per state (spectral/record look best right after the hit)
FREEZE = {"spectral": 0.08, "ring": 0.12, "record": 0.46, "audio": 0.2}


def browser() -> str:
    for b in BROWSERS:
        if pathlib.Path(b).exists():
            return b
    raise SystemExit("no Chrome/Edge found")


def shoot(state: str, scale: float) -> pathlib.Path:
    freeze = FREEZE.get(state, 0.34)
    OUT.mkdir(parents=True, exist_ok=True)
    url = PAGE.as_uri() + "?" + urllib.parse.urlencode({"shot": state, "scale": scale, "freeze": freeze})
    out = OUT / f"{state}@{scale:g}x.png"
    # a private throw-away profile: never touches (or waits on) the user's running browser
    profile = pathlib.Path(tempfile.gettempdir()) / "kickarse-shot-profile"
    cmd = [browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
           "--no-first-run", "--no-default-browser-check", f"--user-data-dir={profile}",
           f"--window-size={int(W * scale)},{int(H * scale)}", "--virtual-time-budget=1500",
           f"--screenshot={out}", url]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=45)
    return out


def main(argv: list[str]) -> None:
    states = argv or SHOTS
    for s in states:
        print(shoot(s, 1).relative_to(ROOT))
    if not argv:
        for s in SHOTS_2X:
            print(shoot(s, 2).relative_to(ROOT))


if __name__ == "__main__":
    main(sys.argv[1:])
