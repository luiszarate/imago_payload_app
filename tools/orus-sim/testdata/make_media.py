#!/usr/bin/env python3
"""Makes orus-sim's media test files (testdata/media/): synthetic aerial photos.

    python3 tools/orus-sim/testdata/make_media.py

Needs Pillow. EO photos carry an EXIF segment written here by hand, with DateTimeOriginal and
an embedded 160x90 thumbnail, the way camera JPEGs usually do; the IR photo has none, so the
apps' fallback (the small photo fetched whole) gets exercised too. Whether the Orus L's own
JPEGs carry a thumbnail is not known yet. The pictures are invented: fields, roads, a river.
"""

import io
import random
import struct
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


def exif_segment(when, thumbnail):
    """APP1 'Exif' segment: IFD0 with DateTime and the Exif IFD (DateTimeOriginal), IFD1 with
    the thumbnail. Little-endian TIFF."""
    stamp = when.encode() + b"\0"  # "YYYY:MM:DD HH:MM:SS" + NUL = 20 bytes
    ifd0_entries, exif_entries, ifd1_entries = 2, 1, 3
    ifd0 = 8
    exif = ifd0 + 2 + 12 * ifd0_entries + 4
    ifd1 = exif + 2 + 12 * exif_entries + 4
    data = ifd1 + 2 + 12 * ifd1_entries + 4
    date0, date1, thumb = data, data + 20, data + 40

    def entry(tag, kind, count, value):
        return struct.pack("<HHI", tag, kind, count) + struct.pack("<I", value)

    tiff = b"II*\0" + struct.pack("<I", ifd0)
    tiff += struct.pack("<H", ifd0_entries) + entry(0x0132, 2, 20, date0) + entry(0x8769, 4, 1, exif)
    tiff += struct.pack("<I", ifd1)
    tiff += struct.pack("<H", exif_entries) + entry(0x9003, 2, 20, date1) + struct.pack("<I", 0)
    tiff += struct.pack("<H", ifd1_entries) + struct.pack("<HHIHH", 0x0103, 3, 1, 6, 0)
    tiff += entry(0x0201, 4, 1, thumb) + entry(0x0202, 4, 1, len(thumbnail)) + struct.pack("<I", 0)
    assert len(tiff) == data
    tiff += stamp + stamp + thumbnail
    body = b"Exif\0\0" + tiff
    return b"\xff\xe1" + struct.pack(">H", len(body) + 2) + body


def jpeg(img, quality):
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=quality, optimize=True)
    return buf.getvalue()


def main():
    OUT.mkdir(exist_ok=True)
    for i, when in enumerate(["2026:10:19 11:02:33", "2026:10:19 09:41:10", "2026:10:16 15:12:48"], start=1):
        img = scene(i, (1280, 720))
        thumb = jpeg(img.resize((160, 90), Image.LANCZOS), 70)
        main_jpeg = jpeg(img, 62)
        (OUT / f"eo{i}.jpg").write_bytes(main_jpeg[:2] + exif_segment(when, thumb) + main_jpeg[2:])
    (OUT / "ir.jpg").write_bytes(jpeg(thermal(7, (640, 512)), 70))
    # Stand-in for the radiometric TIFF: 16-bit grey, no temperatures.
    thermal(8, (160, 128)).convert("L").convert("I").convert("I;16").save(OUT / "ir.tiff")
    for f in sorted(OUT.iterdir()):
        print(f.name, f.stat().st_size)


if __name__ == "__main__":
    main()
