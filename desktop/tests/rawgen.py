#!/usr/bin/env python3
"""Synthetic RAW photos for the tests: a CR2 of the Canon EOS 5D Mark II and DNG files, with real sensor data in them (a Bayer mosaic
or linear RGB, lossless JPEG or uncompressed, strips or tiles, little or big endian), made from a picture. The sensor values are the
picture turned back through the colour pipeline of the program (matrix, white balance, black and white level), so what the program
develops must look like the picture again. Every file also carries the picture as the JPEG preview that the camera would have made
(the program fits the tone of its RAW to it).

Only for tests: needs numpy and pillow. The module is imported by make_samples.py and by run_tests.py."""
import io
import struct

import numpy as np
from PIL import Image

# ------------------------------------------------------------------ colour: the same numbers as desktop/src/fd_raw.h
C5D2 = np.array([[0.4716, 0.0603, -0.0830], [-0.7798, 1.5474, 0.2480], [-0.1496, 0.1937, 0.6651]])      # XYZ -> camera (dcraw)
S2X = np.array([[0.412453, 0.357580, 0.180423], [0.212671, 0.715160, 0.072169], [0.019334, 0.119193, 0.950227]])
CAM_FROM_SRGB = C5D2 @ S2X
CAM_FROM_SRGB = CAM_FROM_SRGB / CAM_FROM_SRGB.sum(axis=1, keepdims=True)           # linear sRGB -> white balanced camera RGB

BLACK = 1024
WHITE = 15600
WB = np.array([2.0, 1.0, 1.5])                                                       # multipliers R G B of the "shot" (exact in 16.16)
META = dict(make="Canon", model="Canon EOS 5D Mark II", dt="2026:10:20 12:34:56", iso=400, exp=(1, 250), fn=(28, 10))


def srgb_to_linear(rgb8):
    v = rgb8.astype(np.float64) / 255.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def camera_rgb(rgb8):
    """the picture as the camera sees it: unit scale 0..1 = black..white level (before the white balance multipliers)"""
    cam = srgb_to_linear(rgb8) @ CAM_FROM_SRGB.T
    return cam / WB[None, None, :]


def to_raw_units(unit, black=BLACK, white=WHITE):
    return np.clip(np.rint(black + unit * (white - black)), 0, white).astype(np.uint16)


def mosaic(rgb8, pattern):
    """a Bayer mosaic (uint16) of the picture; pattern = colours (0 R, 1 G, 2 B) of the pixels at (0,0) (1,0) (0,1) (1,1)"""
    h, w = rgb8.shape[:2]
    cam = camera_rgb(rgb8)
    yy, xx = np.mgrid[0:h, 0:w]
    pat = np.array(pattern).reshape(2, 2)
    chan = pat[yy & 1, xx & 1]
    unit = np.take_along_axis(cam, chan[..., None], axis=2)[..., 0]
    return to_raw_units(unit)


def with_margins(pic_raw, left, top, right, bottom, seed=1):
    """the picture area inside a bigger array whose margins are the masked pixels of the sensor (the black level plus a little noise)"""
    h, w = pic_raw.shape
    rng = np.random.default_rng(seed)
    out = BLACK + rng.integers(-2, 3, (h + top + bottom, w + left + right)).astype(np.int32)
    out[top:top + h, left:left + w] = pic_raw
    return out.astype(np.uint16)


# ------------------------------------------------------------------ lossless JPEG (ITU T.81 process SOF3, Huffman)
def lossless_jpeg(arr, nc, bits=16, ss=1):
    """arr: (rows, width * nc) integers (the components of a pixel next to each other); every difference gets a 5 bit code for its
    category and then the bits of the difference. Returns the bytes of the JPEG."""
    a = arr.astype(np.int64)
    rows, n = a.shape
    w = n // nc
    ra = np.zeros_like(a); ra[:, nc:] = a[:, :-nc]
    rb = np.zeros_like(a); rb[1:, :] = a[:-1, :]
    rc = np.zeros_like(a); rc[1:, nc:] = a[:-1, :-nc]
    pred = {1: ra, 2: rb, 3: rc, 4: ra + rb - rc, 5: ra + ((rb - rc) >> 1), 6: rb + ((ra - rc) >> 1), 7: (ra + rb) >> 1}[ss].copy()
    pred[0, nc:] = ra[0, nc:]                         # the first row: the left neighbour
    pred[0, :nc] = 1 << (bits - 1)
    pred[1:, :nc] = rb[1:, :nc]                       # the first pixel of a row: the pixel above
    d = ((a - pred + 32768) & 0xFFFF) - 32768         # modulo 2^16
    d = d.reshape(-1)
    mag = np.abs(d)
    t = np.zeros(len(d), dtype=np.int64)
    nz = mag > 0
    t[nz] = np.floor(np.log2(mag[nz])).astype(np.int64) + 1
    t[mag == 32768] = 16
    extra = np.where(t == 16, 0, t)
    v = np.where(d >= 0, d, d + (1 << np.minimum(t, 15)) - 1)
    v = v & ((1 << extra) - 1)
    length = 5 + extra
    val = (t << extra) | v
    out = bytearray()
    pend = np.zeros(0, dtype=np.uint8)
    step = 100000
    cols = np.arange(21)[None, :]
    for s in range(0, len(d), step):
        L = length[s:s + step]
        idx = L[:, None] - 1 - cols
        ok = idx >= 0
        bit = ((val[s:s + step, None] >> np.maximum(idx, 0)) & 1).astype(np.uint8)
        stream = np.concatenate([pend, bit[ok]])
        nfull = (len(stream) // 8) * 8
        out += np.packbits(stream[:nfull]).tobytes()
        pend = stream[nfull:]
    if len(pend):
        out += np.packbits(np.concatenate([pend, np.ones(8 - len(pend), dtype=np.uint8)])).tobytes()
    body = bytes(out).replace(b"\xff", b"\xff\x00")
    sof = struct.pack(">HBHHB", 8 + 3 * nc, bits, rows, w, nc) + b"".join(bytes([i + 1, 0x11, 0]) for i in range(nc))
    dht = struct.pack(">HB", 2 + 1 + 16 + 17, 0) + bytes([0] * 4 + [17] + [0] * 11) + bytes(range(17))
    sos = struct.pack(">HB", 6 + 2 * nc, nc) + b"".join(bytes([i + 1, 0]) for i in range(nc)) + bytes([ss, 0, 0])
    return b"\xff\xd8\xff\xc3" + sof + b"\xff\xc4" + dht + b"\xff\xda" + sos + body + b"\xff\xd9"


# ------------------------------------------------------------------ TIFF pieces
def ifd(entries, be, base):
    """one IFD (+ the values that do not fit) that will sit at the file offset base. entries: (tag, type, value): type 1 BYTE / 7 UNDEFINED
    (bytes), 2 ASCII (str), 3 SHORT, 4 LONG (int or list), 5 RATIONAL and 10 SRATIONAL ((num, den) or a list of them)"""
    e = ">" if be else "<"
    entries = sorted(entries, key=lambda x: x[0])
    ifd_len = 2 + 12 * len(entries) + 4
    data = b""
    rows = b""
    for tag, typ, val in entries:
        if typ == 2:
            raw = val.encode("ascii") + b"\0"; cnt = len(raw)
        elif typ in (3, 4):
            vs = val if isinstance(val, (list, tuple)) else [val]
            raw = b"".join(struct.pack(e + ("H" if typ == 3 else "I"), v) for v in vs); cnt = len(vs)
        elif typ in (5, 10):
            vs = val if isinstance(val, list) else [val]
            raw = b"".join(struct.pack(e + ("II" if typ == 5 else "iI"), *v) for v in vs); cnt = len(vs)
        elif typ == -1:                                  # an entry that points somewhere else: val = (type, count, offset)
            rows += struct.pack(e + "HHII", tag, val[0], val[1], val[2])
            continue
        else:
            raw = val; cnt = len(raw)
        if len(raw) <= 4:
            rows += struct.pack(e + "HHI", tag, typ, cnt) + raw.ljust(4, b"\0")
        else:
            rows += struct.pack(e + "HHII", tag, typ, cnt, base + ifd_len + len(data))
            data += raw + (b"\0" if len(raw) % 2 else b"")
    return struct.pack(e + "H", len(entries)) + rows + struct.pack(e + "I", 0) + data


def assemble(builders, start):
    """builders: functions offset -> bytes (an IFD needs to know where it sits); two passes, because the size of a part does not
    depend on the offsets in it. Returns (the bytes of all parts, the list of offsets)"""
    offs = [0] * len(builders)
    for _ in range(2):
        pos = start
        parts = []
        for i, b in enumerate(builders):
            offs[i] = pos
            data = b(offs)
            if len(data) % 2:
                data += b"\0"
            parts.append(data)
            pos += len(data)
    return b"".join(parts), offs


def jpeg_bytes(rgb8, q=95):
    buf = io.BytesIO()
    Image.fromarray(rgb8).save(buf, "JPEG", quality=q, subsampling=2)
    return buf.getvalue()


def rational(x, den=10000):
    return (int(round(x * den)), den)


# ------------------------------------------------------------------ a CR2 of the 5D Mark II
def make_cr2(path, rgb8, orientation=1, slices=2, left=64, top=16, right=8, bottom=0, ss=1):
    """the picture (even width and height) as the sensor data of a CR2: the mosaic G B / R G at the corner of the picture, the masked
    margins around it, a lossless JPEG with two components cut into vertical slices like Canon's, the full size JPEG preview in IFD0."""
    h, w = rgb8.shape[:2]
    raw = with_margins(mosaic(rgb8, [1, 2, 0, 1]), left, top, right, bottom)
    rh, rw = raw.shape
    if slices:
        sw1 = (rw // 2 // (slices + 1)) * 2                  # a slice of this width, `slices` times, then the rest
        sw2 = rw - sw1 * slices
        cuts = [raw[:, i * sw1:(i + 1) * sw1] for i in range(slices)] + [raw[:, slices * sw1:]]
    else:
        sw1 = sw2 = rw
        cuts = [raw]
    stream = np.concatenate([c.reshape(-1) for c in cuts]).reshape(rh, rw)       # slice after slice, each row by row
    ljpg = lossless_jpeg(stream, 2, 14, ss)
    preview = jpeg_bytes(rgb8)
    wbdata = [0] * 1250
    wbdata[0] = 6
    wbdata[63], wbdata[64], wbdata[65], wbdata[66] = int(WB[0] * 1024), 1024, 1024, int(WB[2] * 1024)
    sensor = [34, rw, rh, rw, rh, left, top, left + w - 1, top + h - 1, 0, 0, 0, 0, 0, 0, 0, 0]
    mn_entries = [(0x00E0, 3, sensor), (0x4001, 3, wbdata)]
    mn_size = len(ifd(mn_entries, False, 0))

    def b_ifd0(o):
        return ifd([(0x0100, 4, w), (0x0101, 4, h), (0x0103, 3, 6), (0x010F, 2, META["make"]), (0x0110, 2, META["model"]),
                    (0x0111, 4, o[3]), (0x0112, 3, orientation), (0x0117, 4, len(preview)), (0x0132, 2, META["dt"]),
                    (0x8769, 4, o[1])], False, o[0])

    def b_exif(o):                                                    # the maker note is the next part; its entry points there
        return ifd([(0x829A, 5, META["exp"]), (0x829D, 5, META["fn"]), (0x8827, 3, META["iso"]), (0x927C, -1, (7, mn_size, o[2]))], False, o[1])

    def b_mn(o):
        return ifd(mn_entries, False, o[2])

    def b_ifd3(o):
        return ifd([(0x0103, 3, 6), (0x0111, 4, o[5]), (0x0117, 4, len(ljpg)), (0xC640, 3, [slices, sw1, sw2])], False, o[4])

    body, offs = assemble([b_ifd0, b_exif, b_mn, lambda o: preview, b_ifd3, lambda o: ljpg], 16)
    head = b"II" + struct.pack("<H", 42) + struct.pack("<I", 16) + b"CR" + bytes([2, 0]) + struct.pack("<I", offs[4])
    open(path, "wb").write(head + body)
    return raw


# ------------------------------------------------------------------ a DNG
def make_dng(path, rgb8, kind="strips", be=False, orientation=1, tile=(96, 64), compression=None, black_dim=(2, 2), preview=True,
             pattern=(1, 2, 0, 1), margins=(64, 16, 8, 0), write_neutral=True, preview_rgb=None, extra_active=(0, 0),
             raw_override=None, pic_size=None, preview_jpeg=None, neutral=None, matrix=None, black_values=None):
    """kind: 'strips' (uncompressed, strips of 16 rows), 'tiles' (lossless JPEG, 2 components per tile) or 'linear' (uncompressed
    LinearRaw, 3 samples a pixel, no mosaic). The raw array is the picture plus the masked margins (left, top, right, bottom); the active
    area is the picture. IFD0 is a tiny thumbnail (like Adobe's), SubIFD 0 the raw data, SubIFD 1 the preview JPEG."""
    h, w = pic_size if pic_size else rgb8.shape[:2]
    left, top, right, bottom = margins
    e = ">" if be else "<"
    if raw_override is not None:
        raw = raw_override
        photo, spp = 32803, 1
    elif kind == "linear":
        unit = camera_rgb(rgb8)
        pic = to_raw_units(unit)                                           # (h, w, 3)
        rng = np.random.default_rng(3)
        raw = (BLACK + rng.integers(-2, 3, (h + top + bottom, w + left + right, 3))).astype(np.uint16)
        raw[top:top + h, left:left + w] = pic
        photo, spp = 34892, 3
    else:
        raw = with_margins(mosaic(rgb8, list(pattern)), left, top, right, bottom)
        photo, spp = 32803, 1
    rh, rw = raw.shape[:2]
    comp = compression or (7 if kind == "tiles" else 1)
    if kind == "tiles":
        tw, th = tile
    else:
        tw, th = rw, 16
    across, down = -(-rw // tw), -(-rh // th)
    chunks = []
    for ty in range(down):
        for tx in range(across):
            t = np.zeros((th, tw) + ((spp,) if spp == 3 else ()), dtype=np.uint16)
            part = raw[ty * th:(ty + 1) * th, tx * tw:(tx + 1) * tw]
            t[:part.shape[0], :part.shape[1]] = part
            if kind != "tiles":
                t = t[:part.shape[0]]                                    # the last strip is shorter
            if comp == 7:
                chunks.append(lossless_jpeg(t.reshape(t.shape[0], -1), 2 if spp == 1 else 3, 16, 1 if (tx + ty) % 2 else 6))
            else:
                chunks.append(t.astype(e + "u2").tobytes())
    pv = (preview_jpeg or jpeg_bytes(preview_rgb if preview_rgb is not None else rgb8)) if preview else b""
    thumb = bytes([128]) * 192
    neutral = neutral or [(1, 2), (1, 1), (2, 3)]                           # 1 / (2.0 1.0 1.5)
    mat = C5D2 if matrix is None else matrix
    cm2 = [rational(v) for v in mat.reshape(-1)]
    cm1 = [rational(v * 0.9) for v in mat.reshape(-1)]                      # the matrix of the other illuminant (not the one that is used)
    black = black_values or ([BLACK] * 4 if black_dim == (2, 2) else [BLACK])
    n_chunks = len(chunks)

    def o_ifd0(o):
        sub = [o[1]] + ([o[2]] if preview else [])
        ent = [(0x00FE, 4, 1), (0x0100, 4, 8), (0x0101, 4, 8), (0x0102, 3, [8, 8, 8]), (0x0103, 3, 1), (0x0106, 3, 2),
               (0x010F, 2, META["make"]), (0x0110, 2, META["model"]), (0x0111, 4, o[4]), (0x0112, 3, orientation),
               (0x0115, 3, 3), (0x0116, 4, 8), (0x0117, 4, 192), (0x011C, 3, 1), (0x0132, 2, META["dt"]),
               (0x014A, 4, sub), (0x8769, 4, o[3]),
               (0xC612, 1, bytes([1, 4, 0, 0])), (0xC621, 10, cm1), (0xC622, 10, cm2), (0xC65A, 3, 17), (0xC65B, 3, 21),
               (0xC62A, 10, (25, 100))]
        if write_neutral:
            ent.append((0xC628, 5, neutral))
        return ifd(ent, be, o[0])

    def o_raw(o):
        ex_, ey_ = extra_active
        ent = [(0x00FE, 4, 0), (0x0100, 4, rw), (0x0101, 4, rh), (0x0102, 3, 16), (0x0103, 3, comp), (0x0106, 3, photo),
               (0x0115, 3, spp), (0x011C, 3, 1),
               (0xC61D, 4, WHITE), (0xC68D, 4, [top - ey_, left - ex_, top + h + ey_, left + w + ex_])]
        if kind == "tiles":
            ent += [(0x0142, 4, tw), (0x0143, 4, th), (0x0144, 4, [o[6 + i] for i in range(n_chunks)]), (0x0145, 4, [len(c) for c in chunks])]
        else:
            ent += [(0x0111, 4, [o[6 + i] for i in range(n_chunks)]), (0x0116, 4, th), (0x0117, 4, [len(c) for c in chunks])]
        if spp == 1:
            ent += [(0x828D, 3, [2, 2]), (0x828E, 1, bytes(pattern)), (0xC619, 3, list(black_dim)), (0xC61A, 3, black)]
        else:
            ent += [(0xC61A, 3, [BLACK] * 3)]
        ent += [(0xC61F, 3, [ex_, ey_]), (0xC620, 3, [w, h])]
        return ifd(ent, be, o[1])

    def o_prev(o):
        if not preview:
            return b""
        ent = [(0x00FE, 4, 1), (0x0100, 4, w), (0x0101, 4, h), (0x0102, 3, [8, 8, 8]), (0x0103, 3, 7), (0x0106, 3, 6),
               (0x0111, 4, o[5]), (0x0115, 3, 3), (0x0116, 4, h), (0x0117, 4, len(pv)), (0x011C, 3, 1)]
        return ifd(ent, be, o[2])

    def o_exif(o):
        return ifd([(0x829A, 5, META["exp"]), (0x829D, 5, META["fn"]), (0x8827, 3, META["iso"])], be, o[3])

    builders = [o_ifd0, o_raw, o_prev, o_exif, lambda o: thumb, lambda o: pv]
    builders += [(lambda c: (lambda o: c))(c) for c in chunks]
    body, offs = assemble(builders, 8)
    head = (b"MM\0*" if be else b"II*\0") + struct.pack(e + "I", 8)
    open(path, "wb").write(head + body)
    return raw
