#!/usr/bin/env python3
"""Makes test photos that need no real camera: a full size 5616x3744 JPEG, a small JPEG, two fake CR2 files (a TIFF header
with the JPEG preview inside, like the real ones; one of them marked as a portrait shot, orientation 6), and the same picture
inside fake CR3 files (the box layout of the real Canon CR3: moov / uuid with CMT1, CMT2 / four trak / PRVW / mdat) and DNG
files (the layout of Adobe's: a thumbnail in IFD0, SubIFDs with the raw data and two previews; one big endian, one little
endian, one marked as a portrait shot). The metadata (camera, date, ISO, exposure) is the same in all of them.

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


# ---------------------------------------------------------------- metadata, TIFF and box helpers for the CR3 and DNG fakes
META = dict(make="Canon", model="Canon EOS R5", dt="2026:10:20 12:34:56", iso=400, exp=(1, 250), fn=(28, 10))


def tiff(entries, be=False, base=8):
    """a TIFF (II or MM) with one IFD. entries: (tag, type, value) with type 2 = ASCII (str), 3 = SHORT (int or list), 4 = LONG
    (int or list), 5 = RATIONAL ((n, d)), 1 = BYTE (bytes). Returns the bytes, offsets relative to its start."""
    e = ">" if be else "<"
    entries = sorted(entries, key=lambda x: x[0])
    ifd_len = 2 + 12 * len(entries) + 4
    data = b""
    rows = b""
    for tag, typ, val in entries:
        if typ == 2:
            raw = val.encode("ascii") + b"\0"; cnt = len(raw)
        elif typ == 3:
            vs = val if isinstance(val, (list, tuple)) else [val]; raw = b"".join(struct.pack(e + "H", v) for v in vs); cnt = len(vs)
        elif typ == 4:
            vs = val if isinstance(val, (list, tuple)) else [val]; raw = b"".join(struct.pack(e + "I", v) for v in vs); cnt = len(vs)
        elif typ == 5:
            raw = struct.pack(e + "II", *val); cnt = 1
        else:
            raw = val; cnt = len(raw)
        if len(raw) <= 4:
            rows += struct.pack(e + "HHI", tag, typ, cnt) + raw.ljust(4, b"\0")
        else:
            off = base + ifd_len + len(data)
            rows += struct.pack(e + "HHII", tag, typ, cnt, off)
            data += raw + (b"\0" if len(raw) % 2 else b"")
    return struct.pack(e + "H", len(entries)) + rows + struct.pack(e + "I", 0) + data


def tiff_file(entries, be=False):
    e = ">" if be else "<"
    return (b"MM\0*" if be else b"II*\0") + struct.pack(e + "I", 8) + tiff(entries, be)


def box(typ, payload=b""):
    return struct.pack(">I", 8 + len(payload)) + typ + payload


def fbox(typ, payload=b"", ver=0):
    return box(typ, struct.pack(">I", ver) + payload)


def cmt_blocks(orientation):
    cmt1 = tiff_file([(0x010F, 2, META["make"]), (0x0110, 2, META["model"]), (0x0112, 3, orientation), (0x0132, 2, META["dt"])])
    cmt2 = tiff_file([(0x829A, 5, META["exp"]), (0x829D, 5, META["fn"]), (0x8827, 3, META["iso"])])
    return cmt1, cmt2


def fake_cr3(path, jpeg_path, w, h, orientation, prvw_jpeg_path, jpeg_track=True):
    """the layout of a Canon CR3 (EOS R...): ftyp | moov{uuid(CNCV CCTP CTBO CMT1-4 THMB) mvhd trak1(JPEG) trak2(raw) trak3 trak4} |
    uuid(Canon2) | uuid(PRVW) | mdat (64 bit size). With jpeg_track=False the JPEG track is a track without the 'JPEG' box, so only
    the 1620 x 1080 preview of the PRVW box can be used."""
    jpeg = open(jpeg_path, "rb").read()
    prvw = open(prvw_jpeg_path, "rb").read()
    raw = bytes(range(256)) * 40
    cmt1, cmt2 = cmt_blocks(orientation)

    def trak(tid, width, height, size, offset, is_jpeg):
        stsd_entry = b"\0\0\0\0\0\0\0\1" + b"\0\0\0\0" * 4 + struct.pack(">HH", width, height) + b"\0H\0\0\0H\0\0" + b"\0" * 4 + b"\0\1" + b"\0" * 32 + b"\0\0\0\x18" + b"\xff\xff\0\3\0\0\0\0"
        if is_jpeg:
            stsd_entry += box(b"JPEG", b"\0\0\0\0") + box(b"free", b"\0\0")
        else:
            stsd_entry += box(b"CMP1", b"\0" * 30) + box(b"CDI1", b"\0" * 20)
        stsd = fbox(b"stsd", struct.pack(">I", 1) + box(b"CRAW", stsd_entry))
        stbl = (stsd + fbox(b"stts", struct.pack(">III", 1, 1, 1)) + fbox(b"stsc", struct.pack(">IIII", 1, 1, 1, 1)) +
                fbox(b"stsz", struct.pack(">II", size, 1)) + box(b"free", b"\0" * 7) + fbox(b"co64", struct.pack(">IQ", 1, offset)))
        minf = fbox(b"vmhd", b"\0" * 8) + box(b"dinf", fbox(b"dref", struct.pack(">I", 0))) + box(b"stbl", stbl)
        mdia = fbox(b"mdhd", b"\0" * 20) + fbox(b"hdlr", b"\0" * 4 + b"vide" + b"\0" * 13) + box(b"minf", minf)
        return box(b"trak", fbox(b"tkhd", b"\0" * 80) + box(b"mdia", mdia))

    def build(off_jpeg, off_raw):
        canon = (box(b"CNCV", b"Canon\0" * 5) + box(b"CCTP", b"\0" * 84) + box(b"CTBO", b"\0" * 84) + box(b"free", b"\0\0") +
                 box(b"CMT1", cmt1) + box(b"CMT2", cmt2) + box(b"CMT3", b"\0" * 300) + box(b"CMT4", b"\0" * 200) + box(b"THMB", b"\0" * 44))
        uuid_canon = box(b"uuid", bytes.fromhex("85c0b687820f11e08111f4ce462b6a48") + canon)
        moov = box(b"moov", uuid_canon + fbox(b"mvhd", b"\0" * 100) +
                   trak(1, w, h, len(jpeg), off_jpeg, jpeg_track) + trak(2, w, h, len(raw), off_raw, False) +
                   trak(3, 4, 4, 100, off_raw, False) + trak(4, 4, 4, 100, off_raw, False))
        canon2 = box(b"uuid", bytes.fromhex("210f1687914911e4811100242131fce4") + b"\0" * 300)
        pv = bytes.fromhex("eaf42b5e1c984b88b9fbb7dc406e4d16") + struct.pack(">II", 0, 1) + struct.pack(">I", 8 + 0x14 + len(prvw)) + b"PRVW" + \
            struct.pack(">IHHHHI", 0, 1, 1620, 1080, 1, len(prvw)) + prvw
        return box(b"ftyp", b"crx \0\0\0\1crx isom"), moov, canon2, box(b"uuid", pv)

    ftyp, moov, canon2, pv = build(0, 0)
    mdat_data = len(ftyp) + len(moov) + len(canon2) + len(pv) + 16
    ftyp, moov, canon2, pv = build(mdat_data, mdat_data + len(jpeg))
    mdat = struct.pack(">I", 1) + b"mdat" + struct.pack(">Q", 16 + len(jpeg) + len(raw)) + jpeg + raw
    open(path, "wb").write(ftyp + moov + canon2 + pv + mdat)


def fake_dng(path, jpeg_path, w, h, orientation, preview_small_path, be=False, with_preview=True):
    """the layout of an Adobe DNG: IFD0 = an 8 x 8 uncompressed thumbnail (and the camera, the date, the orientation), SubIFD 0 = the
    raw data (tiled, lossless JPEG, CFA), SubIFD 1 = a 1024 pixel JPEG preview, SubIFD 2 = the big JPEG preview (3000 x 2000).
    With with_preview=False only the thumbnail and the raw data are there (there is nothing the program can develop)."""
    e = ">" if be else "<"
    jpeg = open(jpeg_path, "rb").read()
    small = open(preview_small_path, "rb").read()
    thumb = bytes([128]) * 192
    n_sub = 3 if with_preview else 1
    exif_ifd = tiff([(0x829A, 5, META["exp"]), (0x829D, 5, META["fn"]), (0x8827, 3, META["iso"])], be, 0)

    def ifd_bytes(entries, base):
        return tiff(entries, be, base)

    def build(positions):
        pthumb, praw, psmall, pjpeg, pexif, ifd0_at, sub_at = positions
        ifd0 = [(0x00FE, 4, 1), (0x0100, 4, 8), (0x0101, 4, 8), (0x0102, 3, [8, 8, 8]), (0x0103, 3, 1), (0x0106, 3, 2),
                (0x010F, 2, META["make"]), (0x0110, 2, META["model"]), (0x0111, 4, pthumb), (0x0112, 3, orientation),
                (0x0115, 3, 3), (0x0116, 4, 8), (0x0117, 4, 192), (0x011C, 3, 1), (0x0132, 2, META["dt"]),
                (0x014A, 4, [sub_at[i] for i in range(n_sub)]), (0x8769, 4, pexif), (0xC612, 1, bytes([1, 4, 0, 0]))]
        raw_ifd = [(0x00FE, 4, 0), (0x0100, 4, w), (0x0101, 4, h), (0x0102, 3, 16), (0x0103, 3, 7), (0x0106, 3, 32803),
                   (0x0111, 4, praw), (0x0115, 3, 1), (0x0117, 4, 22)]
        small_ifd = [(0x00FE, 4, 1), (0x0100, 4, 1024), (0x0101, 4, 683), (0x0102, 3, [8, 8, 8]), (0x0103, 3, 7), (0x0106, 3, 6),
                     (0x0111, 4, psmall), (0x0115, 3, 3), (0x0116, 4, 683), (0x0117, 4, len(small)), (0x011C, 3, 1)]
        big_ifd = [(0x00FE, 4, 1), (0x0100, 4, w), (0x0101, 4, h), (0x0102, 3, [8, 8, 8]), (0x0103, 3, 7), (0x0106, 3, 6),
                   (0x0111, 4, pjpeg), (0x0115, 3, 3), (0x0116, 4, h), (0x0117, 4, len(jpeg)), (0x011C, 3, 1)]
        return ifd0, [raw_ifd, small_ifd, big_ifd][:n_sub]

    # two passes: the sizes of the IFDs do not depend on the offsets
    pos = [0, 0, 0, 0, 0, 8, [0, 0, 0]]
    for _ in range(2):
        ifd0, subs = build(pos)
        b0 = ifd_bytes(ifd0, 8)
        at = 8 + len(b0)
        sub_at, sub_bytes = [], []
        for sb in subs:
            sub_at.append(at)
            bs = ifd_bytes(sb, at)
            sub_bytes.append(bs)
            at += len(bs)
        pexif = at
        eb = tiff([(0x829A, 5, META["exp"]), (0x829D, 5, META["fn"]), (0x8827, 3, META["iso"])], be, pexif)
        at += len(eb)
        pthumb = at; at += len(thumb)
        praw = at; at += 22
        psmall = at; at += len(small)
        pjpeg = at; at += len(jpeg)
        pos = [pthumb, praw, psmall, pjpeg, pexif, 8, sub_at + [0, 0, 0]]
    out = (b"MM\0*" if be else b"II*\0") + struct.pack(e + "I", 8) + b0 + b"".join(sub_bytes) + eb + thumb + b"\xff\xd8" + b"\0" * 20
    out += (small if with_preview else b"") + (jpeg if with_preview else b"")
    open(path, "wb").write(out)


RAW_SAMPLES = ["synth_raw_5d2_landscape.cr2", "synth_raw_5d2_portrait.cr2", "synth_raw_strips.dng", "synth_raw_tiles.dng",
               "synth_raw_linear.dng", "synth_raw_crop.dng"]
# CR3 of the Canon EOS RP (crxgen.py codes the sensor data like Canon's CRX): lossless in four tiles, C-RAW without loss (the same sensor values),
# C-RAW with a quantization (a portrait shot, turned by the orientation mark), and a camera that the program has no colour matrix of (JPEG route).
# For each of the first three the exact sensor array that the decoder has to give back is saved too (_cr3_<name>.u16: 16 bit little endian).
CR3_SAMPLES = ["synth_raw_cr3_lossless.cr3", "synth_raw_cr3_craw.cr3", "synth_raw_cr3_portrait.cr3", "synth_raw_cr3_unknown.cr3"]
CR3_LOSSY_Q = [3, 12, 12, 18, 6, 6, 12, 0, 0, 6]


def make_raw_samples(out):
    """files with real sensor data (rawgen.py): a CR2 of the 5D Mark II (landscape, and 'rotated' = portrait), DNGs (uncompressed strips,
    lossless tiles in big endian, linear RGB, a bigger active area that is cropped). All carry the same picture, so the program has to
    develop the same pixels from the CR2 and from every Bayer DNG."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rawgen as rg
    rgb = np.asarray(picture(600, 400, 11))
    rg.make_cr2(os.path.join(out, RAW_SAMPLES[0]), rgb, 1)
    rg.make_cr2(os.path.join(out, RAW_SAMPLES[1]), rgb, 6)
    rg.make_dng(os.path.join(out, RAW_SAMPLES[2]), rgb, kind="strips")
    rg.make_dng(os.path.join(out, RAW_SAMPLES[3]), rgb, kind="tiles", be=True)
    rg.make_dng(os.path.join(out, RAW_SAMPLES[4]), rgb, kind="linear")
    rg.make_dng(os.path.join(out, RAW_SAMPLES[5]), rgb, kind="strips", margins=(64, 16, 16, 8), extra_active=(4, 2))
    Image.fromarray(rgb).save(os.path.join(out, "_raw_picture.png"))               # (the picture itself; run_tests.py compares with it)
    import crxgen
    exact = {}
    exact[CR3_SAMPLES[0]] = crxgen.make_cr3(os.path.join(out, CR3_SAMPLES[0]), rgb, levels=0, tile=(320, 206))
    exact[CR3_SAMPLES[1]] = crxgen.make_cr3(os.path.join(out, CR3_SAMPLES[1]), rgb, levels=3, ctmd=False)        # the maker note of the Canon box only
    exact[CR3_SAMPLES[2]] = crxgen.make_cr3(os.path.join(out, CR3_SAMPLES[2]), rgb, levels=3, qs=CR3_LOSSY_Q, orientation=6)
    crxgen.make_cr3(os.path.join(out, CR3_SAMPLES[3]), rgb, levels=0, tile=(320, 206), model="Canon EOS R99")
    for name, arr in exact.items():
        arr.astype("<u2").tofile(os.path.join(out, "_cr3_" + name[len("synth_raw_cr3_"):-4] + ".u16"))


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
    prvw = picture(1620, 1080, 4)
    save_jpeg(prvw, os.path.join(out, "_tmp_prvw.jpg"), 85)
    small_prev = picture(1024, 683, 5)
    save_jpeg(small_prev, os.path.join(out, "_tmp_small.jpg"), 85)
    jp, pv, sm = (os.path.join(out, x) for x in ("_tmp_preview.jpg", "_tmp_prvw.jpg", "_tmp_small.jpg"))
    fake_cr3(os.path.join(out, "synth_landscape.cr3"), jp, 3000, 2000, 1, pv)
    fake_cr3(os.path.join(out, "synth_portrait.cr3"), jp, 3000, 2000, 6, pv)
    fake_cr3(os.path.join(out, "synth_prvw_only.cr3"), jp, 3000, 2000, 1, pv, jpeg_track=False)
    fake_dng(os.path.join(out, "synth_landscape.dng"), jp, 3000, 2000, 1, sm, be=True)
    fake_dng(os.path.join(out, "synth_portrait.dng"), jp, 3000, 2000, 6, sm, be=False)
    for t in ("_tmp_preview.jpg", "_tmp_prvw.jpg", "_tmp_small.jpg"):
        os.remove(os.path.join(out, t))
    make_raw_samples(out)
    for f in sorted(os.listdir(out)):
        print("%-28s %8d bytes" % (f, os.path.getsize(os.path.join(out, f))))


if __name__ == "__main__":
    main()
