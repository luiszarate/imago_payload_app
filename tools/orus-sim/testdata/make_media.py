#!/usr/bin/env python3
"""Makes orus-sim's media test files (testdata/media/): synthetic aerial photos.

    python3 tools/orus-sim/testdata/make_media.py

Needs Pillow. Like the Orus L's own JPEGs (field report 2026-10-20) they carry no embedded
thumbnail; the simulator pads the EO ones to the real ~10 MB and dates every file through
Last-Modified, so these carry no EXIF date either (a reused picture would bring a wrong one).
The pictures are invented: fields, roads, a river.
"""

import io
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent / "media"


def scene(seed, size):
    rnd = random.Random(seed)
    w, h = size
    img = Image.new("RGB", size, (86, 104, 58))
    d = ImageDraw.Draw(img)
    greens = [(96, 118, 62), (122, 134, 70), (150, 140, 88), (78, 96, 54), (168, 156, 104), (110, 92, 60)]
    # Field patchwork on a slightly rotated grid.
    cell = w // 7
    for gy in range(-1, h // cell + 2):
        for gx in range(-1, w // cell + 2):
            x0 = gx * cell + rnd.randint(-cell // 5, cell // 5)
            y0 = gy * cell + rnd.randint(-cell // 5, cell // 5)
            pts = [(x0, y0), (x0 + cell + rnd.randint(-20, 20), y0 + rnd.randint(-15, 15)),
                   (x0 + cell + rnd.randint(-20, 20), y0 + cell + rnd.randint(-20, 20)),
                   (x0 + rnd.randint(-15, 15), y0 + cell + rnd.randint(-20, 20))]
            d.polygon(pts, fill=rnd.choice(greens))
            for k in range(rnd.randint(0, 9)):  # crop rows
                y = y0 + k * cell // 10
                d.line([(x0, y), (x0 + cell, y + rnd.randint(-4, 4))], fill=(70, 84, 46), width=2)
    # A river and two roads.
    river = [(rnd.randint(0, w // 3), 0)]
    for i in range(1, 9):
        river.append((river[-1][0] + rnd.randint(-w // 12, w // 8), i * h // 8))
    d.line(river, fill=(58, 82, 96), width=max(8, w // 60), joint="curve")
    y = rnd.randint(h // 4, 3 * h // 4)
    d.line([(0, y), (w, y + rnd.randint(-h // 5, h // 5))], fill=(176, 170, 156), width=max(5, w // 140))
    x = rnd.randint(w // 2, w - w // 6)
    d.line([(x, 0), (x + rnd.randint(-w // 6, w // 6), h)], fill=(150, 146, 136), width=max(4, w // 200))
    # Trees and a few vehicles.
    for _ in range(rnd.randint(60, 140)):
        cx, cy, r = rnd.randint(0, w), rnd.randint(0, h), rnd.randint(w // 220, w // 90)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(46, 64, 36))
    for _ in range(rnd.randint(1, 4)):
        cx, cy = x + rnd.randint(-10, 10), rnd.randint(0, h)
        d.rectangle([cx - w // 300, cy - w // 150, cx + w // 300, cy + w // 150],
                    fill=rnd.choice([(220, 220, 224), (180, 30, 40), (30, 40, 60)]))
    return img.filter(ImageFilter.GaussianBlur(0.6))


def thermal(seed, size):
    rnd = random.Random(seed)
    base = scene(seed, size).convert("L").filter(ImageFilter.GaussianBlur(2))
    d = ImageDraw.Draw(base)
    for _ in range(rnd.randint(2, 5)):  # warm spots: vehicles, people
        cx, cy, r = rnd.randint(0, size[0]), rnd.randint(0, size[1]), rnd.randint(4, 14)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=250)
    # A white-hot ramp from black through purple and orange to white.
    ramp = []
    for i in range(256):
        t = i / 255
        ramp += [int(255 * min(1, t * 1.6)), int(255 * max(0, t * 1.4 - 0.5)), int(255 * max(0, min(1, (0.6 - abs(t - 0.35)) * 1.3)))]
    img = base.convert("P")
    img.putpalette(ramp)
    return img.convert("RGB")


def jpeg(img, quality):
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=quality, optimize=True)
    return buf.getvalue()


def main():
    OUT.mkdir(exist_ok=True)
    for f in OUT.iterdir():
        f.unlink()
    # eoN and irN are the same scene, as the two cameras of one shot see it.
    for i in range(1, 4):
        (OUT / f"eo{i}.jpg").write_bytes(jpeg(scene(i, (1280, 720)), 62))
        (OUT / f"ir{i}.jpg").write_bytes(jpeg(thermal(i, (640, 512)), 70))
    for f in sorted(OUT.iterdir()):
        print(f.name, f.stat().st_size)


if __name__ == "__main__":
    main()
