"""Kickarse font pipeline: variable Archivo -> static, subset TTFs for NanoVG.

Why this exists
---------------
NanoVG renders text through fontstash + stb_truetype, which
  * cannot instance variable fonts (it would always draw the default master),
  * ignores OpenType GSUB features (no 'tnum', 'case', 'zero' switching at runtime),
  * does apply basic GPOS pair kerning (stb_truetype >= 1.19).
So every width/weight we use must be baked into its own static TTF, and any feature we want
must already be the default glyph mapping. Archivo's default figures are tabular (all digits
~576 units wide), so readouts do not jitter without freezing 'tnum'.

Roles (see docs/design/DESIGN.md, "Typography"):
    ArchivoSC-Medium.ttf    wdth 87.5  wght 500   labels, menus, buttons, tooltips
    ArchivoSC-SemiBold.ttf  wdth 87.5  wght 620   values, selected states, preset name
    ArchivoExp-SemiBold.ttf wdth 125   wght 600   hero readouts (Depth value, rate display)

The wordmark is NOT a runtime font: tools/assets/logo.py converts glyph outlines to paths.

Licence: Archivo is OFL-1.1 with no Reserved Font Name (see resources/fonts/OFL.txt), so
instancing and subsetting are permitted; the instances keep the Archivo family name with a
width/weight style name and the OFL notice travels with them.

Usage:  python tools/assets/fonts.py        (deterministic; re-run after changing ROLES/CHARS)
Needs:  pip install --user fonttools
"""
from __future__ import annotations

import io
import pathlib

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "resources" / "fonts" / "src" / "Archivo[wdth,wght].ttf"
OUT = ROOT / "resources" / "fonts"

ROLES = [
    # file name,              wdth,  wght, family name,            style name
    ("ArchivoSC-Medium.ttf",    87.5, 500, "Archivo SemiCondensed", "Medium"),
    ("ArchivoSC-SemiBold.ttf",  87.5, 620, "Archivo SemiCondensed", "SemiBold"),
    ("ArchivoExp-SemiBold.ttf", 125.0, 600, "Archivo Expanded",     "SemiBold"),
]

# Basic Latin + Latin-1 + the typographic symbols the UI actually prints.
CHARS = (
    [chr(c) for c in range(0x20, 0x7F)]
    + [chr(c) for c in range(0xA0, 0x100)]
    + list("–—‘’“”•…−′″°×±⁄")
)
KEEP_FEATURES = ["kern", "mark", "mkmk", "liga", "case", "tnum", "zero"]


def set_names(font: TTFont, family: str, style: str) -> None:
    name = font["name"]
    full = f"{family} {style}"
    ps = (family.replace(" ", "") + "-" + style).replace(" ", "")
    for rec in list(name.names):
        if rec.nameID in (16, 17, 21, 22, 25):
            name.removeNames(nameID=rec.nameID)
    for pid, eid, lid in ((3, 1, 0x409), (1, 0, 0)):
        name.setName(family, 1, pid, eid, lid)
        name.setName(style if style in ("Regular", "Bold") else "Regular", 2, pid, eid, lid)
        name.setName(full, 4, pid, eid, lid)
        name.setName(ps, 6, pid, eid, lid)
        name.setName(family, 16, pid, eid, lid)
        name.setName(style, 17, pid, eid, lid)
    uid = f"Kickarse build;{ps}"
    name.setName(uid, 3, 3, 1, 0x409)


def build_one(fname: str, wdth: float, wght: float, family: str, style: str) -> pathlib.Path:
    vf = TTFont(SRC)
    static = instancer.instantiateVariableFont(vf, {"wdth": wdth, "wght": wght}, inplace=False)
    set_names(static, family, style)
    static["OS/2"].usWeightClass = int(round(wght / 10.0) * 10)
    static["OS/2"].usWidthClass = {87.5: 4, 100.0: 5, 125.0: 7}.get(wdth, 5)

    buf = io.BytesIO()
    static.save(buf)
    buf.seek(0)
    font = TTFont(buf)

    opts = subset.Options()
    opts.layout_features = KEEP_FEATURES
    opts.hinting = False            # stb_truetype does not hint; saves ~30 %
    opts.name_IDs = ["*"]
    opts.name_languages = ["*"]
    opts.notdef_outline = True
    opts.glyph_names = False
    opts.legacy_kern = True         # also emit a 'kern' table when possible (belt and braces)
    sub = subset.Subsetter(opts)
    sub.populate(text="".join(CHARS))
    sub.subset(font)

    out = OUT / fname
    font.save(out)
    return out


def coverage_report(path: pathlib.Path) -> str:
    f = TTFont(path)
    cmap = f.getBestCmap()
    missing = [c for c in CHARS if ord(c) not in cmap]
    return "all glyphs present" if not missing else "missing: " + " ".join(f"U+{ord(c):04X}" for c in missing)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for role in ROLES:
        p = build_one(*role)
        print(f"{p.name:26s} {p.stat().st_size/1024:6.1f} KB  {coverage_report(p)}")


if __name__ == "__main__":
    main()
