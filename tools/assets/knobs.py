"""Kickarse knob bodies: a tiny physically based renderer for surfaces of revolution.

What it renders
---------------
Straight-on (orthographic) views of two knob bodies, lit by ONE key light from the upper left
(the same light the vector UI assumes: highlights on top edges, shadows fall down and slightly
right), a weak cool fill from the lower right, and a studio environment (a large softbox up-left,
a dim ceiling, a dark floor) for reflections.

    knob_hero@2x.png   Depth knob: glazed bone ceramic cap on a spun gunmetal collar.
    knob_small@2x.png  Small knobs: soft-touch graphite rubber skirt, spun gunmetal insert.

Only the *body* is baked. Everything that moves or carries meaning (pointer line, value ring,
ticks, band colour) is drawn as vectors at runtime. Because the bodies are rotationally
symmetric and the light does not move when a real knob turns, one static image per size is
physically correct: no filmstrip, no rotating highlights, ~30 KB per knob instead of MBs.

Pixels outside the body contain only the baked contact shadow + ambient occlusion as black with
alpha, so the image composites onto any panel colour with plain source-over blending.

Pipeline per knob
-----------------
1. Profile: the (r, z) half-section is a union of signed-distance shapes (rounded boxes with a
   draft angle, a concave dish). The height field h(r) = highest z inside the profile.
2. Normals come from dh/dr analytically; material id from which part owns the top surface.
3. Shading (linear light, then sRGB):
     diffuse  Lambert * albedo (key + fill + hemispherical ambient * AO)
     specular isotropic GGX (ceramic glaze, rubber sheen) or anisotropic GGX for spun metal,
              where grooves run circumferentially, so highlights stretch radially
     env      analytic softbox environment, edge softness scaled by roughness (pre-filter proxy)
     fresnel  Schlick
4. Ambient occlusion: horizon-based, 16 directions x 12 steps on the supersampled height field.
5. Key-light shadow: height-field ray march with penumbra estimate (soft area light).
6. 4x4 supersampling, box-filtered down, straight (non-premultiplied) alpha PNG.

Every constant is deterministic, so `python tools/assets/knobs.py` always reproduces the files.
The @2x images are drawn at half size at 100 % UI scale (mipmapped) and 1:1 at 200 %.
"""
from __future__ import annotations

import dataclasses
import pathlib

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "resources" / "images"

SS = 4  # supersampling per axis

# Scene lighting (screen space: +x right, +y DOWN, +z towards the viewer).
KEY_DIR = np.array([-0.34, -0.78, 0.90])
KEY_DIR = KEY_DIR / np.linalg.norm(KEY_DIR)
KEY_RGB = np.array([1.00, 0.97, 0.92]) * 1.05     # warm-neutral studio key
FILL_DIR = np.array([0.55, 0.60, 0.58])
FILL_DIR = FILL_DIR / np.linalg.norm(FILL_DIR)
FILL_RGB = np.array([0.78, 0.86, 1.00]) * 0.10    # cool bounce from the lower right
AMB_RGB = np.array([0.95, 0.95, 1.00]) * 0.14     # hemispherical ambient (scaled by AO)
EXPOSURE = 1.0

SOFTBOX_DIR = np.array([-0.40, -0.80, 0.75])
SOFTBOX_DIR = SOFTBOX_DIR / np.linalg.norm(SOFTBOX_DIR)


def tonemap(x: np.ndarray) -> np.ndarray:
    """Narkowicz ACES fit: soft shoulder so glaze highlights roll off instead of clipping."""
    x = x * EXPOSURE
    return np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0)


def bilinear(img: np.ndarray, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    n = img.shape[0]
    x = np.clip(x, 0, n - 1.001)
    y = np.clip(y, 0, n - 1.001)
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    a = img[y0, x0] * (1 - fx) + img[y0, x0 + 1] * fx
    b = img[y0 + 1, x0] * (1 - fx) + img[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def srgb_encode(lin: np.ndarray) -> np.ndarray:
    lin = np.clip(lin, 0.0, 1.0)
    return np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1.0 / 2.4) - 0.055)


def srgb_decode(c: np.ndarray) -> np.ndarray:
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, np.power((c + 0.055) / 1.055, 2.4))


def hexlin(h: str) -> np.ndarray:
    h = h.lstrip("#")
    return srgb_decode(np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]))


# ---------------------------------------------------------------------------------------------
# Profile (r, z) signed distance helpers. Units: 1.0 = outer radius of the knob.

def sd_round_box(r, z, r0, r1, z0, z1, rad, draft=0.0):
    """Box [r0,r1]x[z0,z1] in the (r,z) half-plane with corner radius rad.
    draft > 0 narrows the box towards the top (r1 shrinks by draft * (z - z0))."""
    r = r + draft * np.maximum(z - z0, 0.0)
    cr, cz = 0.5 * (r0 + r1), 0.5 * (z0 + z1)
    hr, hz = 0.5 * (r1 - r0) - rad, 0.5 * (z1 - z0) - rad
    qr, qz = np.abs(r - cr) - hr, np.abs(z - cz) - hz
    outside = np.hypot(np.maximum(qr, 0), np.maximum(qz, 0))
    inside = np.minimum(np.maximum(qr, qz), 0)
    return outside + inside - rad


@dataclasses.dataclass
class Part:
    name: str
    material: str
    r1: float           # outer radius at the base
    z1: float           # top height
    rad: float          # top edge fillet radius
    draft: float = 0.0  # narrowing per unit height
    dish_r: float = 0.0     # concave dish radius on the top face (0 = flat)
    dish_depth: float = 0.0


@dataclasses.dataclass
class KnobSpec:
    name: str
    body_px_1x: float       # diameter of the knob body at 100 % UI scale
    canvas_px_1x: int       # PNG canvas at 100 % (the file is 2x this)
    parts: list
    groove_r: float = 0.0   # radius of a thin dark groove (insert edge), 0 = none
    groove_w: float = 0.0


def height_table(spec: KnobSpec, n_r=6000, n_z=600, r_max=1.6, z_max=1.0):
    """h(r) = top of the union of parts. A coarse downward scan brackets the surface, then 40
    bisection steps refine it; without the refinement dh/dr is quantised and the glaze shows
    'sand' speckles."""
    r = np.linspace(0.0, r_max, n_r)
    z = np.linspace(z_max, 0.0, n_z)
    dz = z[0] - z[1]
    R, Z = np.meshgrid(r, z)
    best_h = np.zeros(n_r)
    owner = np.full(n_r, -1)
    for i, p in enumerate(spec.parts):
        sdf = lambda rr, zz: sd_round_box(rr, zz, -1.0, p.r1, -0.5, p.z1, p.rad, p.draft)
        inside = sdf(R, Z) <= 0.0
        any_in = inside.any(axis=0)
        first = np.argmax(inside, axis=0)
        lo = z[first]                       # inside
        hi = np.minimum(lo + dz, z_max)     # outside (or the scan top)
        for _ in range(40):
            mid = 0.5 * (lo + hi)
            ins = sdf(r, mid) <= 0.0
            lo = np.where(ins, mid, lo)
            hi = np.where(ins, hi, mid)
        h = np.where(any_in, lo, 0.0)
        if p.dish_r > 0:
            u = np.clip(r / p.dish_r, 0.0, 1.0)
            dish = p.dish_depth * (1.0 - u * u) ** 2   # smooth, zero slope at the rim
            h = np.where(any_in & (r < p.dish_r), h - dish, h)
        take = h > best_h + 1e-9
        best_h = np.where(take, h, best_h)
        owner = np.where(take, i, owner)
    return r, best_h, owner


# ---------------------------------------------------------------------------------------------
# Shading

def ggx_iso(nh, alpha):
    a2 = alpha * alpha
    d = nh * nh * (a2 - 1.0) + 1.0
    return a2 / (np.pi * d * d + 1e-12)


def ggx_aniso(nh, th, bh, at, ab):
    d = (th / at) ** 2 + (bh / ab) ** 2 + nh ** 2
    return 1.0 / (np.pi * at * ab * d * d + 1e-12)


def smith_g1(nx, alpha):
    a2 = alpha * alpha
    return 2.0 * nx / (nx + np.sqrt(a2 + (1.0 - a2) * nx * nx) + 1e-12)


def schlick(f0, cos_t):
    return f0 + (1.0 - f0) * (1.0 - cos_t)[..., None] ** 5


def environment(R, rough):
    """Radiance seen along reflection direction R (…,3). A softbox, a ceiling, a dark floor."""
    soft = 0.035 + 0.55 * rough
    # softbox: angular rectangle around SOFTBOX_DIR
    up = np.array([0.0, 0.0, 1.0])
    ax = np.cross(SOFTBOX_DIR, up)
    ax /= np.linalg.norm(ax)
    ay = np.cross(ax, SOFTBOX_DIR)
    u = R @ ax
    v = R @ ay
    w = R @ SOFTBOX_DIR
    in_u = 1.0 - smoothstep(0.30 - soft, 0.30 + soft, np.abs(u))
    in_v = 1.0 - smoothstep(0.42 - soft, 0.42 + soft, np.abs(v))
    box = in_u * in_v * (w > 0)
    ceiling = 0.22 * np.clip(R[..., 2], 0, 1) ** 1.5
    floor_bounce = 0.02 * np.clip(-R[..., 1], 0, 1)
    rim = 0.10 * smoothstep(0.55, 0.9, R[..., 0] * 0.7 + R[..., 1] * 0.7) * (R[..., 2] > 0)
    rad = box[..., None] * np.array([15.5, 15.0, 14.2]) + (ceiling + floor_bounce)[..., None] * np.array([0.9, 0.92, 1.0])
    rad += rim[..., None] * np.array([0.70, 0.82, 1.0])
    return rad


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


MATERIALS = {
    # albedo (linear), specular F0, roughness, metallic, anisotropy (alpha_t, alpha_b) or None
    "ceramic": dict(albedo=hexlin("#CEC7B8"), f0=np.array([0.045] * 3), rough=0.12, aniso=None, env_gain=1.0),
    "gunmetal": dict(albedo=hexlin("#1B1B1D"), f0=hexlin("#5E5F63"), rough=0.30, aniso=(0.08, 0.42), env_gain=0.9),
    "rubber": dict(albedo=hexlin("#232322"), f0=np.array([0.035] * 3), rough=0.62, aniso=None, env_gain=0.35),
}


def render(spec: KnobSpec):
    px2 = spec.canvas_px_1x * 2
    N = px2 * SS
    body_r_px = spec.body_px_1x * 2 * SS / 2.0  # radius in supersampled pixels

    rt, ht, owner_t = height_table(spec)
    dh = np.gradient(ht, rt)

    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float64)
    dx, dy = (xx - c) / body_r_px, (yy - c) / body_r_px
    r = np.hypot(dx, dy)
    h = np.interp(r, rt, ht)
    slope = np.interp(r, rt, dh)
    own = owner_t[np.clip(np.searchsorted(rt, r), 0, len(rt) - 1)]
    body = h > 1e-6

    inv_r = 1.0 / np.maximum(r, 1e-6)
    ux, uy = dx * inv_r, dy * inv_r   # radial unit vector
    n = np.stack([-slope * ux, -slope * uy, np.ones_like(r)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)

    # --- ambient occlusion (horizon based) on the height field, in radius units
    hpx = h * body_r_px
    ao = np.zeros_like(h)
    dirs = 16
    steps = 12
    max_dist = 0.55 * body_r_px
    for k in range(dirs):
        a = 2 * np.pi * (k + 0.5) / dirs
        vx, vy = np.cos(a), np.sin(a)
        horizon = np.zeros_like(h)
        for s in range(1, steps + 1):
            t = max_dist * (s / steps) ** 1.6
            rise = (bilinear(hpx, xx + vx * t, yy + vy * t) - hpx) / t
            horizon = np.maximum(horizon, rise)
        ao += 1.0 - np.sin(np.arctan(np.maximum(horizon, 0.0)))
    ao /= dirs
    ao = np.clip(ao, 0.0, 1.0)

    # --- soft key-light shadow by height-field ray march (penumbra estimate)
    lxy = KEY_DIR[:2] / np.linalg.norm(KEY_DIR[:2])
    tan_el = KEY_DIR[2] / np.linalg.norm(KEY_DIR[:2])
    shadow = np.ones_like(h)
    k_pen = 5.0
    for s in range(1, 64):
        t = 0.9 * body_r_px * (s / 63.0) ** 1.3
        ray_z = hpx + 0.004 * body_r_px + t * tan_el
        gap = (ray_z - bilinear(hpx, xx + lxy[0] * t, yy + lxy[1] * t)) / t
        shadow = np.minimum(shadow, np.clip(k_pen * gap + 0.35, 0.0, 1.0))
    shadow = smoothstep(0.0, 1.0, shadow)

    # --- shading of the body
    V = np.array([0.0, 0.0, 1.0])
    col = np.zeros(h.shape + (3,))
    nv = np.clip(n[..., 2], 1e-4, 1.0)
    tangent = np.stack([-uy, ux, np.zeros_like(r)], axis=-1)  # circumferential (groove direction)
    for mi, part in enumerate(spec.parts):
        mask = body & (own == mi)
        if not mask.any():
            continue
        m = MATERIALS[part.material]
        nm, tm = n[mask], tangent[mask]
        bm = np.cross(nm, tm)
        nvm = nv[mask]
        out = np.zeros((mask.sum(), 3))
        for Ldir, Lrgb, occl in ((KEY_DIR, KEY_RGB, shadow[mask]), (FILL_DIR, FILL_RGB, np.ones(mask.sum()))):
            nl = np.clip(nm @ Ldir, 0.0, 1.0)
            H = Ldir + V
            H = H / np.linalg.norm(H)
            nh = np.clip(nm @ H, 0.0, 1.0)
            if m["aniso"] is None:
                D = ggx_iso(nh, m["rough"] ** 2 + 0.02)
                G = smith_g1(nl, m["rough"] ** 2) * smith_g1(nvm, m["rough"] ** 2)
            else:
                at, ab = m["aniso"]
                D = ggx_aniso(nh, tm @ H, np.einsum("ij,j->i", bm, H), at, ab)
                G = smith_g1(nl, 0.5 * (at + ab)) * smith_g1(nvm, 0.5 * (at + ab))
            F = schlick(m["f0"], np.clip(H @ V, 0, 1) * np.ones_like(nl))
            spec_term = (D * G / (4.0 * nvm + 1e-6))[:, None] * F
            diff = (1.0 - F) * m["albedo"] / np.pi * np.pi  # energy: Lambert with pi folded into light units
            out += (diff * nl[:, None] + spec_term * nl[:, None]) * Lrgb * occl[:, None]
        # ambient + environment reflection
        out += m["albedo"] * AMB_RGB * ao[mask][:, None]
        R = 2.0 * nvm[:, None] * nm - V
        if m["aniso"] is None:
            env = environment(R, m["rough"])
        else:
            # radial blur of the environment: average a few reflection vectors spread along the
            # radial (bitangent) direction, which is where spun grooves smear reflections
            env = np.zeros_like(R)
            spread = np.linspace(-1.0, 1.0, 7)
            for sp in spread:
                Rj = R + bm * (sp * m["aniso"][1] * 0.9)
                Rj /= np.linalg.norm(Rj, axis=1, keepdims=True)
                env += environment(Rj, m["aniso"][0])
            env /= len(spread)
        Fe = schlick(m["f0"], nvm)
        out += env * Fe * m["env_gain"] * (0.35 + 0.65 * ao[mask][:, None])
        col[mask] = out

    # thin dark groove (e.g. where a metal insert meets the rubber)
    if spec.groove_r > 0:
        g = np.exp(-((r - spec.groove_r) / (spec.groove_w * 0.5)) ** 2)
        col *= (1.0 - 0.85 * g)[..., None]

    # --- compose: body opaque, panel = black with alpha from shadow & AO
    panel_dark = 1.0 - (0.42 * (1.0 - shadow) + 0.58 * (1.0 - ao)) * 1.0
    panel_alpha = np.clip((1.0 - panel_dark) * 0.92, 0.0, 1.0)
    panel_alpha *= smoothstep(1.55, 1.0, r)  # keep the shadow inside the canvas, soft edge
    srgb = srgb_encode(tonemap(col))
    alpha = np.where(body, 1.0, panel_alpha)
    rgb = np.where(body[..., None], srgb, 0.0)

    # box-filter down (premultiplied), then un-premultiply
    pre = rgb * alpha[..., None]
    pre = pre.reshape(px2, SS, px2, SS, 3).mean(axis=(1, 3))
    a = alpha.reshape(px2, SS, px2, SS).mean(axis=(1, 3))
    straight = np.where(a[..., None] > 1e-4, pre / np.maximum(a[..., None], 1e-4), 0.0)
    img = np.dstack([straight, a])
    return (np.clip(img, 0, 1) * 255.0 + 0.5).astype(np.uint8)


HERO = KnobSpec(
    name="knob_hero",
    body_px_1x=72.0,
    canvas_px_1x=96,
    parts=[
        # spun gunmetal collar sitting on the panel
        Part("collar", "gunmetal", r1=1.00, z1=0.12, rad=0.04),
        # glazed ceramic cap: tapered wall, generous fillet, shallow dish
        Part("cap", "ceramic", r1=0.90, z1=0.58, rad=0.17, draft=0.12, dish_r=0.62, dish_depth=0.05),
    ],
)

SMALL = KnobSpec(
    name="knob_small",
    body_px_1x=30.0,
    canvas_px_1x=44,
    parts=[
        Part("skirt", "rubber", r1=1.00, z1=0.50, rad=0.24, draft=0.10),
        Part("insert", "gunmetal", r1=0.64, z1=0.515, rad=0.03),
    ],
    groove_r=0.655,
    groove_w=0.05,
)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for spec in (HERO, SMALL):
        img = render(spec)
        path = OUT / f"{spec.name}@2x.png"
        Image.fromarray(img, "RGBA").save(path, optimize=True)
        print(f"{path.name:22s} {img.shape[1]}x{img.shape[0]}  {path.stat().st_size / 1024:.1f} KB")


if __name__ == "__main__":
    main()
