"""Run the prototype's interaction self-test (index.html?test=1) in headless Chrome and print it.

The test dispatches synthetic mouse/keyboard events through the prototype's real listeners and
checks the resulting model state (knob drags, resets, type-in, node add/move/bend/delete, marquee,
undo/redo, rate grid, library, quick shift, line and pencil tools, threshold, crossover, presets,
capture). Exit code 1 on any failure.

Usage: python tools/assets/selftest.py
"""
from __future__ import annotations

import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from screenshots import PAGE, browser  # noqa: E402


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8")
    url = PAGE.as_uri() + "?test=1"
    profile = pathlib.Path(tempfile.gettempdir()) / "kickarse-shot-profile"
    res = subprocess.run([browser(), "--headless=new", "--disable-gpu", "--no-first-run", f"--user-data-dir={profile}",
                          "--window-size=1100,700", "--virtual-time-budget=4000", "--dump-dom", url],
                         capture_output=True, text=True, encoding="utf-8", timeout=90)
    m = re.search(r'<pre id="log">(.*?)</pre>', res.stdout, re.S)
    text = m.group(1) if m else "no log found\n" + res.stdout[-2000:]
    text = text.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
    print(text)
    return 0 if text.startswith("SELFTEST OK") else 1


if __name__ == "__main__":
    sys.exit(main())
