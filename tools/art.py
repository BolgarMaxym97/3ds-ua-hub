"""The Hub's HOME Menu art: the «Вузол» icon chosen in the design (board H3).

    icon.png    48x48: the blue tile, yellow hub and six nodes, no UA (too small to read)
    banner.png  256x128: the top screen while the app is selected on the HOME Menu
    banner.wav  the banner's sound: a short soft two-note chime (as 3ds-ua's installer)

The HOME Menu draws icons without transparency, so the tile fills the whole square. Text is
Nunito 800, the app's own UI font (OFL, app/assets/fonts), instanced by `make fonts`.

Usage (`make art`):
    .venv/bin/python tools/art.py build/fonts/Nunito-800.ttf build/art
"""

from __future__ import annotations

import math
import struct
import sys
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FLAG_BLUE = (0x00, 0x57, 0xB7)
FLAG_YELLOW = (0xFF, 0xD5, 0x00)
WHITE = (0xFF, 0xFF, 0xFF)
INK = (0x1B, 0x24, 0x33)
MUTED = (0x4D, 0x58, 0x69)
GROUND = (0xFF, 0xFF, 0xFF)
SCALE = 8  # drawn large, scaled down: smooth edges at 48 px

NODES = ((50, 14), (81, 32), (81, 68), (50, 86), (19, 68), (19, 32))


def mark(size: int, font: Path | None, rounded: bool) -> Image.Image:
    """The «Вузол» tile, `size` px square. With `font`, UA sits in the hub (large sizes only)."""
    s = size * SCALE
    im = Image.new("RGB", (s, s), GROUND if rounded else FLAG_BLUE)
    d = ImageDraw.Draw(im)
    if rounded:
        d.rounded_rectangle([0, 0, s - 1, s - 1], round(s * 0.26), fill=FLAG_BLUE)
    small = font is None
    k = s / 100 * 0.82
    c = s / 2

    def at(x: float, y: float) -> tuple[float, float]:
        return c + (x - 50) * k, c + (y - 50) * k

    for x, y in NODES:
        d.line([at(50, 50), at(x, y)], fill=WHITE, width=round((6 if small else 4) * k))
    for x, y in NODES:
        nx, ny = at(x, y)
        r = (9 if small else 7) * k
        d.ellipse([nx - r, ny - r, nx + r, ny + r], fill=FLAG_YELLOW)
    r = 20 * k
    d.ellipse([c - r, c - r, c + r, c + r], fill=FLAG_YELLOW)
    if font is not None:
        face = ImageFont.truetype(str(font), round(20 * k))
        left, top, right, bottom = d.textbbox((0, 0), "UA", font=face)
        d.text((c - (right - left) / 2 - left, c - (bottom - top) / 2 - top), "UA", font=face, fill=FLAG_BLUE)
    return im.resize((size, size), Image.LANCZOS)


def banner(font: Path) -> Image.Image:
    """The H3 banner: the tile, «3DS UA Hub», «українізатори ігор», the flag strip below."""
    w, h = 256, 128
    im = Image.new("RGB", (w * SCALE, h * SCALE), GROUND)
    d = ImageDraw.Draw(im)
    title = ImageFont.truetype(str(font), 22 * SCALE)
    sub = ImageFont.truetype(str(font), 13 * SCALE)
    d.text((94 * SCALE, 40 * SCALE), "3DS UA Hub", font=title, fill=INK)
    d.text((95 * SCALE, 69 * SCALE), "українізатори ігор", font=sub, fill=MUTED)
    d.rectangle([0, (h - 8) * SCALE, w * SCALE, (h - 4) * SCALE], fill=FLAG_BLUE)
    d.rectangle([0, (h - 4) * SCALE, w * SCALE, h * SCALE], fill=FLAG_YELLOW)
    im = im.resize((w, h), Image.LANCZOS)
    im.paste(mark(70, font, rounded=True), (14, 25))
    return im


def chime(path: Path) -> None:
    """Two soft notes (E5, A5) with a quick decay, 0.6 s, 16-bit mono."""
    rate, length = 32000, 0.6
    frames = bytearray()
    for i in range(int(rate * length)):
        t = i / rate
        v = 0.0
        for start, freq in ((0.0, 659.25), (0.12, 880.0)):
            if t >= start:
                v += math.sin(2 * math.pi * freq * (t - start)) * math.exp(-(t - start) * 7)
        frames += struct.pack("<h", int(v * 0.25 * 32767))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(frames))


def main() -> None:
    font, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    mark(48, None, rounded=False).save(out / "icon.png")
    banner(font).save(out / "banner.png")
    chime(out / "banner.wav")
    print(f"{out}: icon.png, banner.png, banner.wav")


if __name__ == "__main__":
    main()
