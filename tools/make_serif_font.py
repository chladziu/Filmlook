#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_serif_font.py <Bethany_Elingston.otf> <title font.ttf> <films.index.json> <out serif_data.h>

Draws the serif letters of the panel (one bitmap per letter, 4 levels of grey = 2 bits per pixel) so that the camera can
print them without any font engine:
  big / mid   film names (two sizes: the long names use the smaller one)
  med         tabs, row names, values, kind of the film
  sml         description, key help, version
  title       the whole text "Film Look Simulation" as one picture (heavy retro serif, free font Caprasimo, OFL)
Polish letters the font does not have (ą ć ę ń ś ź ż and capitals) are composed from the letter and the mark of the font
(acute, dot above, ogonek); their place is copied from the font's own ó / Ó.
Kerning of the font (GPOS) is stored once, in 1/1000 em.
Needs Pillow (with raqm), numpy, fontTools.
"""
import json, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

K = 8                                   # supersampling
PAD = 12                                # canvas margin in px around the glyph
LEVELS = 4
DROP = 0.10                             # capital accents are moved down by this part of the font size (tight line spacing)

POLISH = {"ą": "a", "ć": "c", "ę": "e", "ń": "n", "ś": "s", "ź": "z", "ż": "z",
          "Ą": "A", "Ć": "C", "Ę": "E", "Ń": "N", "Ś": "S", "Ź": "Z", "Ż": "Z"}
MARK = {"ć": "acute", "ń": "acute", "ś": "acute", "ź": "acute", "Ć": "acute", "Ń": "acute", "Ś": "acute", "Ź": "acute",
        "ż": "dot", "Ż": "dot", "ą": "ogonek", "ę": "ogonek", "Ą": "ogonek", "Ę": "ogonek"}


class Face:
    def __init__(self, path, size, gamma=1.0, track=0.0, vary=None, stroke=0):
        self.path, self.size, self.gamma, self.track = path, size, gamma, track
        self.stroke = stroke                                    # hi-res px added to every side of the letters: faux bold
        self.f = ImageFont.truetype(path, size * K, layout_engine=ImageFont.Layout.RAQM)
        if vary:
            axes = self.f.get_variation_axes(); vals = []
            for a in axes:
                n = a["name"] if isinstance(a["name"], str) else a["name"].decode()
                key = {"Weight": "wght", "Softness": "SOFT", "Optical Size": "opsz", "Optical size": "opsz", "Wonk": "WONK"}.get(n, n)
                vals.append(vary.get(key, a["default"]))
            self.f.set_variation_by_axes(vals)
        self.tt = TTFont(path)
        self.upm = self.tt["head"].unitsPerEm
        self.by = K * int(size * 1.15)                           # baseline row of the hi-res canvas (room for accents on capitals)
        self.cm = self.tt.getBestCmap()
        self.cache = {}

    def has(self, ch):
        return ord(ch) in self.cm

    def hi(self, text):
        """hi-res coverage (0..1) of text on a canvas with the origin at (PAD, baseline = PAD + asc/K) in px"""
        w = int(self.f.getlength(text, features=["-liga", "-aalt"])) + 2 * PAD * K
        h = self.by + int(self.size * K * 0.45)
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((PAD * K, self.by), text, font=self.f, fill=255, anchor="ls", features=["-liga", "-aalt"],
                                stroke_width=self.stroke, stroke_fill=255)
        return np.asarray(im, dtype=np.float32) / 255

    def base_y(self):                    # baseline row on the hi-res canvas
        return self.by

    def adv(self, ch):                   # advance in em/1000 (kerning not included)
        ch = POLISH.get(ch, ch) if ord(ch) not in self.cm else ch
        return self.tt["hmtx"][self.cm[ord(ch)]][0] * 1000.0 / self.upm + 2.0 * self.stroke * 1000.0 / (K * self.size)


def ink(a):
    ys, xs = np.nonzero(a > 0.02)
    return (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1) if len(xs) else None


def place(base, mark, dx, dy):
    """base with mark pasted (max) at an offset of dx, dy hi-res pixels; the canvas of base is kept"""
    out = base.copy()
    h, w = base.shape
    mh, mw = mark.shape
    x0, y0 = max(dx, 0), max(dy, 0)
    x1, y1 = min(w, dx + mw), min(h, dy + mh)
    if x1 > x0 and y1 > y0:
        out[y0:y1, x0:x1] = np.maximum(out[y0:y1, x0:x1], mark[y0 - dy:y1 - dy, x0 - dx:x1 - dx])
    return out


def compose(face, ch):
    """hi-res image of a Polish letter that the font lacks"""
    base = POLISH[ch]
    a = face.hi(base)
    bb = ink(a)
    mk = MARK[ch]
    low = base.islower()
    ref_plain, ref_acc = ("o", "ó") if low else ("O", "Ó")
    p, q = face.hi(ref_plain), face.hi(ref_acc)
    acc = np.clip(q - p, 0, 1)                                    # the acute of the font's own ó / Ó
    ab = ink(acc); pb = ink(p)
    cxb = (bb[0] + bb[2]) / 2
    if mk == "acute":
        cx_ref = (ab[0] + ab[2]) / 2 - (pb[0] + pb[2]) / 2        # where the acute sits relative to the letter centre
        dx = int(round(cxb + cx_ref - (ab[0] + ab[2]) / 2))
        # same height as in ó: top of the letter does not matter for c n s z (x-height), caps share the cap height
        top_shift = bb[1] - pb[1] + (0 if low else int(DROP * face.size * K))     # accents on capitals sit lower than in the font
        return place(a, acc, dx, top_shift)
    if mk == "dot":
        d = face.hi("˙")
        db = ink(d)
        gap = pb[1] - ab[3]                                       # distance letter-top -> accent bottom in ó
        dx = int(round(cxb - (db[0] + db[2]) / 2))
        dy = int(round(bb[1] - gap - db[3])) + (0 if low else int(DROP * face.size * K))
        return place(a, d, dx, dy)
    if mk == "ogonek":
        o = face.hi("˛")
        ob = ink(o)
        baseline = face.base_y()
        # hook hangs below the baseline at the right foot of the letter
        right = bb[2]
        dx = int(round(right - (ob[2] - ob[0]) * 0.80 - ob[0]))
        dy = int(round(baseline - ob[1] - 4 * K // 8))
        return place(a, o, dx, dy)
    raise SystemExit("mark?")


def quant(cov, gamma):
    c = np.clip(cov, 0, 1) ** gamma
    return np.round(c * (LEVELS - 1)).astype(np.uint8)


def box(a):
    h, w = a.shape
    h2, w2 = h // K * K, w // K * K
    return a[:h2, :w2].reshape(h2 // K, K, w2 // K, K).mean(axis=(1, 3))


def glyph_bitmap(face, ch, hi):
    """-> (levels array cropped, x offset from the origin, y offset of the top row from the baseline)"""
    # shift so that the origin falls on a pixel boundary: origin x = PAD*K, baseline = base_y (multiple of K needed)
    by = face.base_y()
    pad_y = (-by) % K                                             # make the baseline a multiple of K
    hi = np.pad(hi, ((pad_y, 0), (0, 0)))
    by += pad_y
    cov = box(hi)
    q = quant(cov, face.gamma)
    ys, xs = np.nonzero(q > 0)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return q[y0:y1, x0:x1], int(x0 - PAD), int(y0 - by // K)


def pack(q):
    h, w = q.shape
    stride = (w + 3) // 4
    out = bytearray()
    for r in range(h):
        row = list(q[r]) + [0] * (stride * 4 - w)
        for i in range(stride):
            b = 0
            for k in range(4):
                b = (b << 2) | int(row[i * 4 + k])
            out.append(b)
    return out


def build_font(name, face, chars, extra_glyphs=None):
    glyphs, bits = [], bytearray()
    asc = desc = 0
    for ch in sorted(set(chars), key=ord):
        if ch == " ":
            glyphs.append((ord(ch), int(round(face.adv(" ") * face.size * 16 / 1000)), 0, 0, 0, 0, 0, ord(ch))); continue
        if face.has(ch):
            hi = face.hi(ch)
        elif ch in POLISH:
            hi = compose(face, ch)
        else:
            print("  no glyph for %r in %s" % (ch, name)); continue
        g = glyph_bitmap(face, ch, hi)
        if g is None:
            print("  empty %r" % ch); continue
        q, x, y = g
        h, w = q.shape
        off = len(bits)
        bits += pack(q)
        kc = ord(POLISH.get(ch, ch))
        glyphs.append((ord(ch), int(round(face.adv(ch) * face.size * 16 / 1000)), w, h, x, y, off, kc))
        asc = max(asc, -y); desc = max(desc, y + h)
    return dict(name=name, size=face.size, glyphs=glyphs, bits=bits, asc=asc, desc=desc, track=face.track)


def kerning(face, chars):
    pairs = []
    em = face.size * K
    chars = [c for c in sorted(set(chars), key=ord) if face.has(c) and c != " "]
    ft = ["-liga", "-aalt"]
    singles = {c: face.f.getlength(c, features=ft) for c in chars}
    for a in chars:
        for b in chars:
            k = face.f.getlength(a + b, features=ft) - singles[a] - singles[b]
            v = int(round(k * 1000 / em))
            if abs(v) >= 12:                                  # smaller than 0.4 px at 30 px: not worth a table entry
                pairs.append((ord(a), ord(b), v))
    return pairs


def emit(fonts, kern, out):
    o = ["/* generated by tools/make_serif_font.py - do not edit */\n"]
    for f in fonts:
        n = f["name"]
        o.append("static const unsigned char sfd_bits_%s[] = {" % n)
        b = f["bits"]
        for i in range(0, len(b), 24):
            o.append("  " + ",".join("%d" % x for x in b[i:i + 24]) + ",")
        if not b: o.append("  0")
        o.append("};")
        o.append("static const sf_glyph sfd_g_%s[] = {" % n)
        for (code, adv, w, h, x, y, off, kc) in f["glyphs"]:
            o.append("  { %d, %d, %d, %d, %d, %d, %d, %d }," % (code, adv, w, h, x, y, off, kc))
        o.append("};")
        o.append("static const sf_font sf_%s = { sfd_g_%s, %d, sfd_bits_%s, %d, %d, %d, %d };\n" %
                 (n, n, len(f["glyphs"]), n, f["asc"], f["desc"], f["size"], int(round(f["track"] * 16))))
    o.append("static const unsigned int sfd_kkey[] = {")
    for i in range(0, len(kern), 8):
        o.append("  " + ",".join("0x%X" % ((a << 16) | b) for a, b, v in kern[i:i + 8]) + ",")
    o.append("};")
    o.append("static const short sfd_kval[] = {")
    for i in range(0, len(kern), 16):
        o.append("  " + ",".join("%d" % v for a, b, v in kern[i:i + 16]) + ",")
    o.append("};")
    o.append("#define SFD_KERN_N %d" % len(kern))
    open(out, "w").write("\n".join(o) + "\n")


def main(beth, titlef, idxp, out):
    idx = json.load(open(idxp))
    import string
    name_chars = set("".join(f["name"] + f["brand"] for f in idx)) | set(string.ascii_letters + string.digits + " ‹›.-+/&()'")
    all_chars = [chr(c) for c in range(0x20, 0x7F)] + list("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ·‹›–—…°×")
    SIZES = dict(big=(56, 1.0, 0.0), mid=(46, 1.0, 0.0), med=(30, 0.85, 0.3), sml=(25, 0.78, 0.4), xs=(21, 0.72, 0.4), xxs=(18, 0.65, 0.5))
    fonts = []
    kern = None
    for nm, (sz, gm, tr) in SIZES.items():
        face = Face(beth, sz, gm, tr)
        chars = name_chars if nm in ("big", "mid") else all_chars
        print(nm, sz, len(chars))
        fonts.append(build_font(nm, face, chars))
        if kern is None:
            kern = kerning(face, all_chars)
    # bold (selected tab, selected brand): the same size with a stroke round the letters; only the letters it needs
    bold_chars = name_chars | set("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ")
    bface = Face(beth, 25, 0.78, 0.4, None, 3)
    print("smlb", 25, len(bold_chars))
    fonts.append(build_font("smlb", bface, bold_chars))
    # title: the whole text as one picture
    tface = Face(titlef, 33, 0.9, 0.0)
    hi = tface.hi("Film Look Simulation")
    q, x, y = glyph_bitmap(tface, "T", hi)
    h, w = q.shape
    tit = dict(name="title", size=33, glyphs=[(1, (w + 2) * 16, w, h, x, y, 0, 1)], bits=pack(q), asc=-y, desc=y + h, track=0)
    fonts.append(tit)
    kern.sort()
    emit(fonts, kern, out)
    print("title %dx%d, kerning pairs %d" % (w, h, len(kern)))
    tot = sum(len(f["bits"]) for f in fonts)
    print("bitmap bytes", tot)


if __name__ == "__main__":
    main(*sys.argv[1:5])
