"""Kickarse wordmark: "kickarse" whose last four letters duck like a sidechained signal.

Idea: the wordmark *demonstrates* the product. "kick" sits on the baseline (the trigger);
"arse" is pushed down on the hit and recovers letter by letter along a release curve, so the
word itself draws a duck envelope. It reads as a deliberate typographic gesture, not a mistake,
because the offsets follow one smooth exponential and the first drop is large.

Letterforms: Archivo (OFL, no RFN) at wdth 125 / wght 800, lower case, tracking -1 %.
Using glyph outlines as artwork is allowed by the OFL; no font ships for the logo.

Outputs (resources/images/):
    logo_wordmark.json   path commands in em units (y down, baseline at 0) for NanoVG/Canvas:
                         ["M",x,y] ["L",x,y] ["Q",cx,cy,x,y] ["Z"]; nonzero winding; each contour
                         carries its own winding so NanoVG needs nvgPathWinding(NVG_HOLE) only
                         where "hole" is true (computed from signed area).
    logo_wordmark.svg    same geometry for documents / installer.
    logo_wordmark@2x.png 2x raster (bone on transparent) for places without a vector renderer.

Usage: python tools/assets/logo.py
"""
from __future__ import annotations

import json
import math
import pathlib

import numpy as np
from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "resources" / "fonts" / "src" / "Archivo[wdth,wght].ttf"
OUT = ROOT / "resources" / "images"

TEXT = "kickarse"
WDTH, WGHT = 125.0, 800.0
TRACKING = -0.010          # em
# Duck: drop of each letter in x-height units (y down). "kick" is the trigger; "a" takes the hit
# and "r s e" recover along a linear release back to the baseline. Four variants were compared
# (linear, S-curve, exponential, shallow); the exponential/S versions read as a typo because
# "r" or "s" end up almost on the baseline. The even ramp reads as intentional motion.
DUCK = [0.0, 0.0, 0.0, 0.0, 0.36, 0.24, 0.12, 0.0]


def duck_offsets(n_letters: int) -> list[float]:
    assert n_letters == len(DUCK)
    return list(DUCK)


def quad_flatten(p0, c, p1, steps=8):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * c[0] + t * t * p1[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * c[1] + t * t * p1[1]) for t in np.linspace(0, 1, steps)[1:]]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    vf = TTFont(SRC)
    font = instancer.instantiateVariableFont(vf, {"wdth": WDTH, "wght": WGHT}, inplace=False)
    upm = font["head"].unitsPerEm
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
    xh = font["OS/2"].sxHeight / upm
    offs = duck_offsets(len(TEXT))

    contours = []   # list of dict(cmds=[...], hole=bool)
    pen_x = 0.0
    for i, ch in enumerate(TEXT):
        gname = cmap[ord(ch)]
        pen = DecomposingRecordingPen(gs)
        gs[gname].draw(pen)
        dy = offs[i] * xh
        cur = None
        start = None
        cmds = []
        poly = []
        for op, args in pen.value:
            if op == "moveTo":
                (x, y), = args
                cur = (pen_x + x / upm, -y / upm + dy)
                start = cur
                cmds = [["M", round(cur[0], 5), round(cur[1], 5)]]
                poly = [cur]
            elif op == "lineTo":
                (x, y), = args
                cur = (pen_x + x / upm, -y / upm + dy)
                cmds.append(["L", round(cur[0], 5), round(cur[1], 5)])
                poly.append(cur)
            elif op == "qCurveTo":
                pts = [(pen_x + x / upm, -y / upm + dy) for (x, y) in args]
                # TrueType implied on-curve points between consecutive off-curve points
                for k in range(len(pts) - 1):
                    c = pts[k]
                    if k < len(pts) - 2:
                        e = ((pts[k][0] + pts[k + 1][0]) / 2, (pts[k][1] + pts[k + 1][1]) / 2)
                    else:
                        e = pts[k + 1]
                    cmds.append(["Q", round(c[0], 5), round(c[1], 5), round(e[0], 5), round(e[1], 5)])
                    poly.extend(quad_flatten(cur, c, e))
                    cur = e
            elif op == "closePath" or op == "endPath":
                cmds.append(["Z"])
                area = 0.0
                for a, b in zip(poly, poly[1:] + poly[:1]):
                    area += a[0] * b[1] - b[0] * a[1]
                contours.append({"cmds": cmds, "area": area})
            else:
                raise RuntimeError(op)
        pen_x += font["hmtx"][gname][0] / upm + TRACKING

    # outer contours share one orientation; holes the other
    outer_sign = math.copysign(1.0, max(contours, key=lambda c: abs(c["area"]))["area"])
    for c in contours:
        c["hole"] = math.copysign(1.0, c["area"]) != outer_sign
        del c["area"]

    xs = [v for c in contours for cmd in c["cmds"] for v in cmd[1::2] if isinstance(v, float)]
    ys = [v for c in contours for cmd in c["cmds"] for v in cmd[2::2] if isinstance(v, float)]
    bbox = [min(xs), min(ys), max(xs), max(ys)]
    data = {
        "text": TEXT,
        "units": "em (1.0 = font size); y down; baseline of 'kick' at y = 0",
        "font": f"Archivo wdth {WDTH} wght {WGHT} (OFL)",
        "xHeight": round(xh, 5),
        "duckOffsetsXHeight": [round(o, 4) for o in offs],
        "bbox": [round(v, 5) for v in bbox],
        "contours": contours,
    }
    (OUT / "logo_wordmark.json").write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")

    # SVG
    def to_d(c):
        out = []
        for cmd in c["cmds"]:
            out.append(cmd[0] + " ".join(f"{v:.4f}" for v in cmd[1:]))
        return "".join(out)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{bbox[0]:.4f} {bbox[1]:.4f} {w:.4f} {h:.4f}">'
           f'<path fill="#E6E1D6" fill-rule="nonzero" d="{"".join(to_d(c) for c in contours)}"/></svg>')
    (OUT / "logo_wordmark.svg").write_text(svg, encoding="utf-8")

    # 2x raster for non-vector contexts (height 44 px @2x = 22 px logical)
    scale = 44 / h * 1.0
    ss = 4
    W, H = int(math.ceil(w * scale)) + 4, int(math.ceil(h * scale)) + 4
    img = Image.new("L", (W * ss, H * ss), 0)
    d = ImageDraw.Draw(img)
    # even-odd is fine for raster preview because holes never overlap outers across letters
    for c in contours:
        pts = []
        cur = None
        for cmd in c["cmds"]:
            if cmd[0] == "M":
                cur = (cmd[1], cmd[2]); pts = [cur]
            elif cmd[0] == "L":
                cur = (cmd[1], cmd[2]); pts.append(cur)
            elif cmd[0] == "Q":
                seg = quad_flatten(cur, (cmd[1], cmd[2]), (cmd[3], cmd[4]), 12)
                pts.extend(seg); cur = seg[-1]
        P = [((x - bbox[0]) * scale * ss + 2 * ss, (y - bbox[1]) * scale * ss + 2 * ss) for x, y in pts]
        d.polygon(P, fill=0 if c["hole"] else 255)
    img = img.resize((W, H), Image.LANCZOS)
    rgba = Image.new("RGBA", (W, H), (230, 225, 214, 0))
    rgba.putalpha(img)
    rgba.save(OUT / "logo_wordmark@2x.png", optimize=True)
    print("logo_wordmark.json/.svg/@2x.png  bbox(em)", [round(v, 3) for v in bbox], "offsets", [round(o, 3) for o in offs])


if __name__ == "__main__":
    main()
