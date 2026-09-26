"""Kickarse surface grain: a tileable, signed, blue-ish noise tile for the chassis.

The chassis is a flat vector fill; on 8-bit panels its gentle gradients band, and a perfectly
flat dark grey reads as "web page". A very faint grain dithers the gradients and gives the
powder-coated surface a material without becoming a texture you can *see* as a texture.

Encoding (so it works with plain source-over alpha blending in NanoVG and Canvas 2D):
    noise v in [-1, 1]
    v > 0  ->  RGB = 255 (white), A = v * AMP    (lightens)
    v < 0  ->  RGB =   0 (black), A = -v * AMP   (darkens)
One draw of the tile with imagePattern + a global alpha lightens and darkens symmetrically.

The tile is meant to be mapped 1:1 onto *device* pixels: in NanoVG pass an image extent of
TILE / uiScale so the grain never becomes coarse at 200 %.

Blue-ish: white noise is high-passed (subtract a wrapped Gaussian blur) so there are no
low-frequency clumps; a Gaussian that is wrapped in the FFT domain keeps the tile seamless.

Usage: python tools/assets/textures.py      (deterministic: fixed seed)
"""
from __future__ import annotations

import pathlib

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "resources" / "images"

TILE = 256
AMP = 0.16      # alpha at |v| = 1; the UI multiplies this by its own grain opacity token
SEED = 0x4B1C   # "KICK"-ish, fixed for reproducibility


def wrapped_gaussian_highpass(n: np.ndarray, sigma: float) -> np.ndarray:
    f = np.fft.fft2(n)
    ky = np.fft.fftfreq(n.shape[0])[:, None]
    kx = np.fft.fftfreq(n.shape[1])[None, :]
    g = np.exp(-2.0 * (np.pi * sigma) ** 2 * (kx * kx + ky * ky))
    low = np.real(np.fft.ifft2(f * g))
    return n - low


def grain_tile() -> np.ndarray:
    rng = np.random.default_rng(SEED)
    n = rng.standard_normal((TILE, TILE))
    n = wrapped_gaussian_highpass(n, sigma=1.6)
    n /= np.percentile(np.abs(n), 99.5)
    return np.clip(n, -1.0, 1.0)


def encode_signed(v: np.ndarray, amp: float) -> np.ndarray:
    rgba = np.zeros(v.shape + (4,), dtype=np.uint8)
    pos = v > 0
    rgba[..., 0:3][pos] = 255
    rgba[..., 3] = np.clip(np.abs(v) * amp * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return rgba


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    v = grain_tile()
    Image.fromarray(encode_signed(v, AMP), "RGBA").save(OUT / "grain_256.png", optimize=True)
    print("grain_256.png", (OUT / "grain_256.png").stat().st_size // 1024, "KB")


if __name__ == "__main__":
    main()
