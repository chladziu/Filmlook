#!/usr/bin/env python3
"""Makes test photos that need no real camera: a full size 5616x3744 JPEG, a small JPEG, and two fake CR2 files (a TIFF header
with the JPEG preview inside, like the real ones; one of them marked as a portrait shot, orientation 6).

usage: make_samples.py <output folder>      needs: pip install pillow numpy"""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw


def picture(w, h, seed):
    """a smooth colourful picture with edges, a face-coloured blob and some fine noise (so that grain, curves and the
    resizer all have something to chew on)"""
    rng = np.random.default_rng(seed)
    sw, sh = 351, 234                                        # drawn small, enlarged smoothly
    y, x = np.mgrid[0:sh, 0:sw].astype(np.float32)
    r = 120 + 100 * np.sin(x / sw * 6.0 + 0.5) * np.cos(y / sh * 3.0)
    g = 110 + 90 * np.sin(y / sh * 5.0 + x / sw * 2.0)
    b = 100 + 110 * np.cos(x / sw * 4.0 - y / sh * 4.0)
    base = np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8)
    im = Image.fromarray(base).resize((w, h), Image.BICUBIC)
    d = ImageDraw.Draw(im)
    for i in range(14):                                      # hard edged shapes
        x0, y0 = int(rng.integers(0, w - 200)), int(rng.integers(0, h - 200))
        x1, y1 = x0 + int(rng.integers(60, w // 6)), y0 + int(rng.integers(60, h // 6))
        col = tuple(int(c) for c in rng.integers(0, 256, 3))
        (d.ellipse if i % 2 else d.rectangle)([x0, y0, x1, y1], fill=col)
    d.ellipse([w * 0.40, h * 0.30, w * 0.60, h * 0.62], fill=(224, 172, 140))      # a skin tone
    for k in range(0, w, max(8, w // 90)):                                       # fine lines
        d.line([k, h * 0.7, k + w // 40, h], fill=(20, 20, 20) if (k // 8) % 2 else (240, 240, 240), width=2)
    a = np.asarray(im).astype(np.int16)
    a += rng.integers(-5, 6, a.shape, dtype=np.int16)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


def save_jpeg(im, path, q=92):
    im.save(path, "JPEG", quality=q, subsampling=2, optimize=False, progressive=False)   # 4:2:0, baseline: like the camera's


def fake_cr2(path, jpeg_path, w, h, orientation):
    jpeg = open(jpeg_path, "rb").read()
    entries = [(0x0100, 4, w), (0x0101, 4, h), (0x0103, 3, 6), (0x0111, 4, 0), (0x0112, 3, orientation), (0x0117, 4, len(jpeg))]
    ifd = 16
    joff = ifd + 2 + 12 * len(entries) + 4
    joff = (joff + 15) & ~15
    head = b"II" + struct.pack("<H", 42) + struct.pack("<I", ifd) + b"CR" + bytes([2, 0]) + struct.pack("<I", 0)
    body = struct.pack("<H", len(entries))
    for tag, typ, val in entries:
        if tag == 0x0111:
            val = joff
        body += struct.pack("<HHII", tag, typ, 1, val)
    body += struct.pack("<I", 0)
    data = head + body
    data += b"\0" * (joff - len(data)) + jpeg
    open(path, "wb").write(data)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    big = picture(5616, 3744, 1)
    save_jpeg(big, os.path.join(out, "synth_5616x3744.jpg"))
    small = picture(1200, 800, 2)
    save_jpeg(small, os.path.join(out, "synth_small.jpg"), 88)
    land = picture(3000, 2000, 3)
    save_jpeg(land, os.path.join(out, "_tmp_preview.jpg"))
    fake_cr2(os.path.join(out, "synth_landscape.cr2"), os.path.join(out, "_tmp_preview.jpg"), 3000, 2000, 1)
    fake_cr2(os.path.join(out, "synth_portrait.cr2"), os.path.join(out, "_tmp_preview.jpg"), 3000, 2000, 6)
    os.remove(os.path.join(out, "_tmp_preview.jpg"))
    for f in sorted(os.listdir(out)):
        print("%-28s %8d bytes" % (f, os.path.getsize(os.path.join(out, f))))


if __name__ == "__main__":
    main()
