#!/usr/bin/env python3
"""Builds ML/FONTS/FILM23.RBF: the stock ML 'Argand Normal 23' bitmap font, made 2 px taller and extended with the
Polish letters in the (otherwise unused) character slots 0x80..0x91.  Accents are drawn from the base letters,
so the look matches the rest of the UI.  Usage: make_polish_font.py argnor23.rbf FILM23.RBF [preview.png]"""
import struct, sys

# slot -> (base letter, mark)
POLISH = [
    (0x80, 'a', 'ogonek'), (0x81, 'c', 'acute'), (0x82, 'e', 'ogonek'), (0x83, 'l', 'stroke'),
    (0x84, 'n', 'acute'),  (0x85, 'o', 'acute'), (0x86, 's', 'acute'),  (0x87, 'z', 'acute'),
    (0x88, 'z', 'dot'),
    (0x89, 'A', 'ogonek'), (0x8A, 'C', 'acute'), (0x8B, 'E', 'ogonek'), (0x8C, 'L', 'stroke'),
    (0x8D, 'N', 'acute'),  (0x8E, 'O', 'acute'), (0x8F, 'S', 'acute'),  (0x90, 'Z', 'acute'),
    (0x91, 'Z', 'dot'),
]
EXTRA_ROWS = 2          # new empty rows on top, room for the accents above capitals

def load(path):
    d = open(path, 'rb').read()
    hdr = list(struct.unpack('<11i', d[72:116]))
    charSize, points, height, maxW, cf, cl, unk, wmap, cmap, desc, intl = hdr
    n = cl - cf + 1
    width = 8 * charSize // height
    rb = width // 8
    widths = list(d[wmap:wmap + n])
    glyphs = []
    for i in range(n):
        raw = d[cmap + i * charSize: cmap + (i + 1) * charSize]
        g = [[bool(raw[y * rb + x // 8] & (1 << (x % 8))) for x in range(width)] for y in range(height)]
        glyphs.append(g)
    return d, hdr, widths, glyphs, width, height, cf

def shift_down(g, k, width):
    return [[False] * width for _ in range(k)] + [row[:] for row in g]

def rows_with_pixels(g):
    return [y for y, r in enumerate(g) if any(r)]

def cols_with_pixels(g, y0=None, y1=None):
    ys = range(len(g)) if y0 is None else range(y0, y1 + 1)
    return sorted({x for y in ys for x, v in enumerate(g[y]) if v})

def make_marked(base, w, mark, width, is_cap, xh_top, cap_top, baseline):
    g = [r[:] for r in base]
    cols = cols_with_pixels(g)
    x0, x1 = cols[0], cols[-1]
    cx = (x0 + x1) // 2
    new_w = w
    def put(x, y):
        nonlocal new_w
        if 0 <= y < len(g) and 0 <= x < width:
            g[y][x] = True
            if x + 2 > new_w: new_w = x + 2
    top = cap_top if is_cap else xh_top          # first row of the base letter body
    if mark == 'acute':
        # 4 rows (3 for capitals), 2 px wide diagonal, one empty row above the letter
        rows = 3 if is_cap else 4
        bottom = top - 2
        for i in range(rows):
            y = bottom - (rows - 1) + i
            off = (rows - 1 - i) - (rows - 1) // 2          # top row to the right
            put(cx + off, y); put(cx + off + 1, y)
    elif mark == 'dot':
        y = top - 4 if not is_cap else top - 4
        for dy in (0, 1):
            for dx in (0, 1):
                put(cx + dx, top - 4 + dy + (1 if is_cap else 0) - (0 if is_cap else 0))
    elif mark == 'ogonek':
        # small tail below the right end of the baseline row
        xr = max(x for x, v in enumerate(g[baseline]) if v)
        put(xr - 1, baseline + 1); put(xr, baseline + 1)
        put(xr - 1, baseline + 2); put(xr, baseline + 2)
        put(xr, baseline + 3); put(xr + 1, baseline + 3)
        new_w = max(new_w, xr + 3)
    elif mark == 'stroke':
        # diagonal bar across the letter; make room on the left by shifting the glyph right by 2
        sh = 2
        g = [[False] * sh + r[:width - sh] for r in g]
        y_mid = (top + baseline) // 2 + 1
        for x in range(0, x1 + sh + 3):
            y = y_mid + 2 - (x * 4) // max(1, (x1 + sh + 2))
            for dy in (0, 1):
                if 0 <= y - dy < len(g) and x < width: g[y - dy][x] = True
        new_w = w + sh + 1
    return g, min(new_w, width)

def main(src, dst, preview=None):
    d, hdr, widths, glyphs, width, height, cf = load(src)
    charSize, points, h, maxW, cf_, cl, unk, wmap, cmap, desc, intl = hdr
    ch = lambda c: ord(c) - cf
    # metrics of the stock font
    xh_top = rows_with_pixels(glyphs[ch('x')])[0]
    cap_top = rows_with_pixels(glyphs[ch('H')])[0]
    baseline = rows_with_pixels(glyphs[ch('H')])[-1]
    # all glyphs: 2 empty rows on top
    glyphs = [shift_down(g, EXTRA_ROWS, width) for g in glyphs]
    xh_top += EXTRA_ROWS; cap_top += EXTRA_ROWS; baseline += EXTRA_ROWS
    new_h = height + EXTRA_ROWS
    for slot, base, mark in POLISH:
        g, w = make_marked(glyphs[ch(base)], widths[ch(base)], mark, width, base.isupper(), xh_top, cap_top, baseline)
        glyphs[slot - cf] = g
        widths[slot - cf] = w
    rb = width // 8
    new_charSize = new_h * rb
    out = bytearray()
    name = b'Film23 Polish'
    out += struct.pack('<ii', 0x0DF00EE0, 3) + name.ljust(64, b'\0')
    out += struct.pack('<11i', new_charSize, points, new_h, maxW, cf_, cl, unk, wmap, cmap, desc, intl)
    assert len(out) == 116
    out += bytes(widths)
    assert len(out) == cmap, (len(out), cmap)
    for g in glyphs:
        for row in g:
            for bx in range(rb):
                v = 0
                for b in range(8):
                    if row[bx * 8 + b]: v |= 1 << b
                out.append(v)
    open(dst, 'wb').write(out)
    print('written', dst, len(out), 'bytes; height', new_h, 'xh_top', xh_top, 'cap_top', cap_top, 'baseline', baseline)
    if preview:
        from PIL import Image
        text = ["Film kolorowy negatywowy  \x80\x81\x82\x83\x84\x85\x86\x87\x88",
                "\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91  Ciep\x83e, z\x83ote tony",
                "Czarno-bia\x83y film  \x8c\x8e\x8f\x91 \x80 \x82 pokr\x85t\x83o",
                "Mi\x82kki kontrast  \x8f\x90\x91 ZA\x91\x8c\x8d\x89\x8b\x8a\x8d"]
        scale = 3
        W = 720 * scale // 2; Hh = (new_h + 4) * len(text) * scale
        img = Image.new('RGB', (W, Hh), (0, 0, 0))
        px = img.load()
        for li, t in enumerate(text):
            x = 10
            y0 = li * (new_h + 4) * scale
            for c in t:
                code = ord(c)
                g = glyphs[code - cf]
                for yy in range(new_h):
                    for xx in range(widths[code - cf]):
                        if g[yy][xx]:
                            for sx in range(scale):
                                for sy in range(scale):
                                    if x * 1 + xx * scale + sx < W:
                                        px[x + xx * scale + sx, y0 + yy * scale + sy] = (255, 255, 255)
                x += widths[code - cf] * scale
        img.save(preview)

if __name__ == '__main__':
    main(*sys.argv[1:])
