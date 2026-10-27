#!/usr/bin/env python3
"""A CR3 of the Canon EOS RP with real sensor data in it, for the tests: the picture turned back through the colour pipeline of the
program (like rawgen.py does for CR2 and DNG), then CODED THE WAY CANON'S CRX DOES IT (version 1):

  * lossless (levels 0): every Bayer site is a plane of its own (a quarter of the pixels), predicted like JPEG-LS (median of the left,
    above and corner values), the residuals in Golomb-Rice codes with a parameter that follows the data, runs of equal values in the
    run mode of JPEG-LS;
  * C-RAW (levels 3): three levels of the integer 5/3 wavelet of JPEG 2000 on every plane; the low-pass band coded as above, the
    high-pass bands as zero runs and Rice codes, every band quantized with the step of its parameter q (q = 0: no loss at all, so the
    C-RAW file holds exactly the same sensor values as the lossless one).

This is the other half of src/fd_cr3.h: the encoder is written from the same description of the format, so a file made here and decoded
there must give back every sensor value exactly (the real files of the EOS RP were compared with LibRaw once, by hand). The container is
the one of the real files: ftyp / moov{uuid(Canon: CMT1 ...) trak(JPEG) trak(CRAW: CMP1, CDI1 > IAD1) trak(CTMD)} / mdat.

Only for tests: needs numpy and pillow."""
import struct

import numpy as np

import rawgen as rg

RP_MATRIX = np.array([8608, -2097, -1178, -5425, 13265, 2383, -1149, 2238, 5680], dtype=np.float64).reshape(3, 3) / 10000.0   # XYZ -> camera
RP_BLACK = 2048
RP_WHITE = 14558                      # the specular white of the EOS RP: below what the 14 bit converter can give
RP_BITS = 14
CFA_NAMES = ["RGGB", "GRBG", "GBRG", "BGGR"]
CFA_PATTERNS = [[0, 1, 1, 2], [1, 0, 2, 1], [1, 2, 0, 1], [2, 1, 1, 0]]       # colours (0 R 1 G 2 B) of the cell at the corner of the array


# ------------------------------------------------------------------ bits, Rice codes, runs
class Bits:
    """bits, most significant first"""

    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.nbits = 0

    def put(self, v, n):
        if n == 0:
            return
        self.acc = (self.acc << n) | (v & ((1 << n) - 1))
        self.nbits += n
        while self.nbits >= 8:
            self.nbits -= 8
            self.out.append((self.acc >> self.nbits) & 0xFF)
        self.acc &= (1 << self.nbits) - 1

    def finish(self):
        if self.nbits:
            self.out.append((self.acc << (8 - self.nbits)) & 0xFF)
            self.acc = 0
            self.nbits = 0
        return bytes(self.out)


RUNJ = [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15]


def adapt(k, s):
    """the next Rice parameter"""
    if s >= (3 << k):
        return min(k + 2, 20) if s >= (3 << (k + 1)) else min(k + 1, 20)
    if k > 0 and s < (1 << (k - 1)):
        return k - 1
    return k


def put_rice(b, k, s):
    """a unary quotient (zeros, then a one) and k low bits; a quotient of 41 or more is 41 zeros, a one and the 21 bit value"""
    q = s >> k
    if q >= 41:
        assert s < (1 << 21), "a value that does not fit the escape"
        b.put(1, 42)
        b.put(s, 21)
    else:
        b.put(1, q + 1)
        b.put(s & ((1 << k) - 1), k)


def put_run(b, idx, length, rem):
    """a run of `length` (1..rem) values; idx = [the run index], kept between the runs of a plane"""
    n = 1
    while n < rem:
        j = RUNJ[idx[0]]
        if length - n >= (1 << j):
            b.put(1, 1)
            n += 1 << j
            if n <= rem and idx[0] < 31:
                idx[0] += 1
        else:
            b.put(0, 1)
            b.put(length - n, j)
            if idx[0] > 0:
                idx[0] -= 1
            return
    assert n == length


def median(left, above, corner):
    mx, mn = (left, above) if left > above else (above, left)
    if corner >= mx:
        return mn
    if corner <= mn:
        return mx
    return left + above - corner


def zigzag(e):
    return 2 * e if e >= 0 else -2 * e - 1


def enc_lossless(plane, runs=True):
    """a plane (or the low-pass band) of integers without loss"""
    h, w = plane.shape
    b = Bits()
    k = 0
    idx = [0]
    zero = [0] * w
    rows = plane.astype(np.int64).tolist()
    for y in range(h):
        cur = rows[y]
        prev = rows[y - 1] if y else zero
        left = prev[0]
        x = 0
        while x < w:
            from_above = False
            if x + 1 < w and left == prev[x] and left == prev[x + 1]:
                if runs and cur[x] == left:
                    n = 1
                    while x + n < w and cur[x + n] == left:
                        n += 1
                    b.put(1, 1)
                    put_run(b, idx, n, w - x)
                    x += n
                    if x == w:
                        break
                    from_above = True
                else:
                    b.put(0, 1)
            above = prev[x]
            corner = above if x == 0 else prev[x - 1]
            right = prev[x + 1] if x + 1 < w else above
            pred = above if from_above else median(left, above, corner)
            s = zigzag(cur[x] - pred)
            put_rice(b, k, s)
            left = cur[x]
            est = (s + 2 * abs(right - above)) // 2 if (y > 0 and x + 1 < w) else s
            k = adapt(k, est)
            x += 1
    return b.finish()


def enc_highpass(band, runs=True):
    """a high-pass band: coefficients, mostly zero"""
    h, w = band.shape
    b = Bits()
    k = 0
    idx = [0]
    zero = [0] * w
    rows = band.astype(np.int64).tolist()
    ksp = [0] * w
    for y in range(h):
        cur = rows[y]
        prev = rows[y - 1] if y else zero
        ks = [0] * w
        left = 0
        x = 0
        while x < w:
            nonzero = 0
            if x + 1 < w and left == 0 and prev[x] == 0 and prev[x + 1] == 0:
                if runs and cur[x] == 0:
                    n = 1
                    while x + n < w and cur[x + n] == 0:
                        n += 1
                    b.put(1, 1)
                    put_run(b, idx, n, w - x)
                    x += n
                    if x == w:
                        break
                else:
                    b.put(0, 1)
                nonzero = 1                       # after a run, or instead of one, the coefficient is not 0
            v = cur[x]
            s = zigzag(v) - nonzero
            assert s >= 0
            put_rice(b, k, s)
            left = v
            k = adapt(k, s)
            if y > 0 and x + 1 < w and ksp[x + 1] > k + 1:
                k += 1
            ks[x] = k
            x += 1
        ksp = ks
    return b.finish()


# ------------------------------------------------------------------ the wavelet (forward 5/3, integer)
def fwd1(a):
    """one level along axis 0 of a 2D integer array: (low, high) with ceil(n/2) and floor(n/2) rows; the line is extended symmetrically"""
    n = a.shape[0]
    ev, od = a[0::2], a[1::2]
    ne, nh = ev.shape[0], od.shape[0]
    if nh == 0:
        return ev.copy(), od.copy()
    evn = np.concatenate([ev[1:], ev[-1:]], axis=0)[:nh]
    high = od - ((ev[:nh] + evn) >> 1)
    prev = np.clip(np.arange(ne) - 1, 0, nh - 1)
    cur = np.clip(np.arange(ne), 0, nh - 1)
    low = ev + ((high[prev] + high[cur] + 2) >> 2)
    return low, high


def fwd2d(x):
    """one level of the wavelet: columns first, then rows (the decoder goes the other way round): LL, HL, LH, HH"""
    lo, hi = fwd1(x)
    llt, hlt = fwd1(lo.T)
    lht, hht = fwd1(hi.T)
    return llt.T, hlt.T, lht.T, hht.T


def step(q):
    sc = [40, 45, 51, 57, 64, 72]
    return (sc[q % 6] << (q // 6)) // 64


def quantize(c, q):
    st = step(q)
    if st <= 1:
        return c
    return np.sign(c) * ((np.abs(c) + st // 2) // st)


def inv1(low, high):
    """the inverse of fwd1"""
    ne, nh = low.shape[0], high.shape[0]
    if nh == 0:
        return low.copy()
    prev = np.clip(np.arange(ne) - 1, 0, nh - 1)
    cur = np.clip(np.arange(ne), 0, nh - 1)
    ev = low - ((high[prev] + high[cur] + 2) >> 2)
    evn = np.concatenate([ev[1:], ev[-1:]], axis=0)[:nh]
    od = high + ((ev[:nh] + evn) >> 1)
    out = np.empty((ne + nh,) + low.shape[1:], dtype=low.dtype)
    out[0::2] = ev
    out[1::2] = od
    return out


def inv2d(ll, hl, lh, hh):
    top = inv1(ll.T, hl.T).T
    bottom = inv1(lh.T, hh.T).T
    return inv1(top, bottom)


def dequantize(c, q):
    return c * max(step(q), 1)


def plane_after_quantization(plane, qs):
    """what the decoder has to give back for a plane that was coded with the quantization parameters qs: the plane through the wavelet,
    the bands quantized and scaled back, through the inverse wavelet"""
    x = plane.astype(np.int64)
    levels = []
    for _ in range(3):
        ll, hl, lh, hh = fwd2d(x)
        levels.append((hl, lh, hh))
        x = ll
    x = dequantize(quantize(x, qs[0]), qs[0])
    i = 1
    deq = {}
    for lev in (2, 1, 0):
        deq[lev] = []
        for bnd in levels[lev]:
            deq[lev].append(dequantize(quantize(bnd, qs[i]), qs[i]))
            i += 1
    for lev in (2, 1, 0):
        x = inv2d(x, *deq[lev])
    return x


def enc_craw_plane(plane, qs):
    """the 10 bands of a plane: the low-pass band, then the three high-pass bands (HL, LH, HH) of the coarsest level, of the middle one,
    of the finest. Returns the list of the bytes of the bands"""
    x = plane.astype(np.int64)
    levels = []
    for _ in range(3):
        ll, hl, lh, hh = fwd2d(x)
        levels.append((hl, lh, hh))
        x = ll
    bands = [enc_lossless(quantize(x, qs[0]))]
    i = 1
    for lev in (2, 1, 0):
        for bnd in levels[lev]:
            bands.append(enc_highpass(quantize(bnd, qs[i])))
            i += 1
    return bands


# ------------------------------------------------------------------ the sample of the image track
def build_sample(raw, bits, tile_w, tile_h, levels, qs):
    """raw: the sensor array (height, width) of uint16. Returns (the header block, the data): the sample is header + data"""
    height, width = raw.shape
    mid = 1 << (bits - 1)
    across = -(-width // tile_w)
    down = -(-height // tile_h)
    headers = b""
    data = b""
    for ty in range(down):
        for tx in range(across):
            tile = raw[ty * tile_h:(ty + 1) * tile_h, tx * tile_w:(tx + 1) * tile_w].astype(np.int64) - mid
            tile_data = b""
            plane_hdrs = b""
            for p in range(4):
                sub = tile[p // 2::2, p % 2::2]
                bands = [enc_lossless(sub)] if levels == 0 else enc_craw_plane(sub, qs)
                band_hdrs = b"".join(struct.pack(">HHII", 0xFF03, 8, len(bd), (qs[i] if levels else 0) << 19) for i, bd in enumerate(bands))
                plane_data = b"".join(bands)
                plane_hdrs += struct.pack(">HHII", 0xFF02, 8, len(plane_data), 0) + band_hdrs
                tile_data += plane_data
            headers += struct.pack(">HHII", 0xFF01, 8, len(tile_data), 0) + plane_hdrs
            data += tile_data
    return headers, data


# ------------------------------------------------------------------ the container
def box(typ, payload=b""):
    return struct.pack(">I", 8 + len(payload)) + typ + payload


def fbox(typ, payload=b"", ver=0):
    return box(typ, struct.pack(">I", ver) + payload)


def tiff_le(entries):
    return b"II*\0" + struct.pack("<I", 8) + rg.ifd(entries, False, 8)


def craw_entry(w, h, children):
    """a sample entry 'CRAW': a visual sample entry (78 bytes), 4 bytes of its own, the boxes"""
    body = (b"\0\0\0\0\0\0\0\1" + b"\0" * 16 + struct.pack(">HH", w, h) + struct.pack(">II", 0x480000, 0x480000) + b"\0" * 4 + b"\0\1" +
            b"\0" * 32 + struct.pack(">HH", 0x18, 0xFFFF) + b"\0\1\0\1" + children)
    return box(b"CRAW", body)


def trak(entry, size, offset, handler=b"vide"):
    stsd = fbox(b"stsd", struct.pack(">I", 1) + entry)
    stbl = (stsd + fbox(b"stts", struct.pack(">III", 1, 1, 1)) + fbox(b"stsc", struct.pack(">IIII", 1, 1, 1, 1)) +
            fbox(b"stsz", struct.pack(">II", size, 1)) + box(b"free", b"\0" * 7) + fbox(b"co64", struct.pack(">IQ", 1, offset)))
    minf = fbox(b"vmhd", b"\0" * 8) + box(b"dinf", fbox(b"dref", struct.pack(">I", 0))) + box(b"stbl", stbl)
    mdia = fbox(b"mdhd", b"\0" * 20) + fbox(b"hdlr", b"\0" * 4 + handler + b"\0" * 13) + box(b"minf", minf)
    return box(b"trak", fbox(b"tkhd", b"\0" * 80) + box(b"mdia", mdia))


def color_data(version=17, wb=(2048, 1024, 1024, 1536), black=(RP_BLACK,) * 4, white=RP_WHITE):
    """the ColorData of the maker note (a record of 16 bit words; the places are those of versions 16 to 19)"""
    cd = [0] * 900
    cd[0] = version
    cd[71:75] = list(wb)
    cd[329:333] = list(black)
    cd[797] = white
    return cd


def maker_note(cd):
    return tiff_le([(0x4001, 3, cd)])


def ctmd_sample(note):
    """the timed metadata: records (a length, a type, 6 more bytes), items inside the records of the types 7 to 9 (a length, a tag, the data)"""
    def rec(kind, items):
        body = b"".join(struct.pack("<II", 8 + len(d), tag) + d for tag, d in items)
        return struct.pack("<IH", 12 + len(body), kind) + b"\0" * 6 + body
    return rec(1, []) + rec(7, [(0x8769, b"\0" * 16), (0x927C, note)]) + rec(8, [(0x1234, b"\0" * 4)])


def make_cr3(path, rgb8, levels=0, tile=None, cfa=3, margins=(17, 9, 3, 1), ctmd=True, orientation=1, qs=None, model="Canon EOS RP",
             cd=None, runs=True, noise_seed=1):
    """the picture (even width and height) as a CR3. The sensor array is the picture plus the masked margins (left, top, right, bottom;
    the sum with the picture must be even), its Bayer pattern `cfa` (0 RGGB, 1 GRBG, 2 GBRG, 3 BGGR) is that of the array corner, so the
    pattern at the corner of the picture follows from the margins. tile = (width, height) of a tile (even), None = one tile.
    ctmd=False leaves out the timed metadata: the maker note is then the Canon box CMT3's only.
    qs = the quantization parameters of the 10 bands of a C-RAW plane (default: 0 = no loss).
    Returns the sensor array (uint16, height x width) that the decoder has to give back (the picture itself when nothing is lost)."""
    h, w = rgb8.shape[:2]
    left, top, right, bottom = margins
    rw, rh = w + left + right, h + top + bottom
    assert rw % 2 == 0 and rh % 2 == 0
    base = CFA_PATTERNS[cfa]
    pattern = [base[(((top + (p >> 1)) & 1) << 1) | ((left + (p & 1)) & 1)] for p in range(4)]        # at the corner of the picture
    mat = RP_MATRIX @ rg.S2X
    mat = mat / mat.sum(axis=1, keepdims=True)
    cam = rg.srgb_to_linear(rgb8) @ mat.T / rg.WB[None, None, :]
    yy, xx = np.mgrid[0:h, 0:w]
    chan = np.array(pattern).reshape(2, 2)[yy & 1, xx & 1]
    unit = np.take_along_axis(cam, chan[..., None], axis=2)[..., 0]
    pic = rg.to_raw_units(unit, RP_BLACK, RP_WHITE)
    rng = np.random.default_rng(noise_seed)
    raw = RP_BLACK + rng.integers(-2, 3, (rh, rw)).astype(np.int32)
    raw[top:top + h, left:left + w] = pic
    raw = raw.astype(np.uint16)
    tile_w, tile_h = tile if tile else (rw, rh)
    qs = list(qs) if qs is not None else [0] * 10
    headers, data = build_sample(raw, RP_BITS, tile_w, tile_h, levels, qs)
    sample = headers + data
    if levels and any(qs):                                  # lossy: what comes back is the picture through the quantization
        mid = 1 << (RP_BITS - 1)
        want = np.zeros_like(raw)
        for ty in range(-(-rh // tile_h)):
            for tx in range(-(-rw // tile_w)):
                tile = raw[ty * tile_h:(ty + 1) * tile_h, tx * tile_w:(tx + 1) * tile_w].astype(np.int64) - mid
                got = np.zeros(tile.shape, dtype=np.int64)
                for p in range(4):
                    got[p // 2::2, p % 2::2] = plane_after_quantization(tile[p // 2::2, p % 2::2], qs)
                want[ty * tile_h:(ty + 1) * tile_h, tx * tile_w:(tx + 1) * tile_w] = np.clip(got + mid, 0, (1 << RP_BITS) - 1)
        expected = want
    else:
        expected = raw

    cmp1 = bytearray(52)
    cmp1[0:6] = bytes([0xFF, 0, 0, 0x30, 0x01, 0])
    cmp1[8:12] = struct.pack(">I", rw); cmp1[12:16] = struct.pack(">I", rh)
    cmp1[16:20] = struct.pack(">I", tile_w); cmp1[20:24] = struct.pack(">I", tile_h)
    cmp1[24] = RP_BITS; cmp1[25] = (4 << 4) | cfa; cmp1[26] = levels; cmp1[27] = 0x80
    cmp1[28:32] = struct.pack(">I", len(headers))
    for i in range(4):
        cmp1[36 + 4 * i:40 + 4 * i] = bytes([1, 1, 0, 0])
    words = [0] * 28                                           # the areas, the way of the real files: the crop (8..11), the optical black, the active area (20..23)
    words[2], words[3], words[4], words[5], words[6] = rw, rh, 1, 2, 1
    words[8:12] = [left, top, left + w - 1, top + h - 1]
    words[12:16] = [0, 0, max(left - 1, 0), rh - 1]
    words[16:20] = [max(left, 0), 0, rw - 1, max(top - 1, 0)]
    words[20:24] = [max(left - 4, 0), max(top - 4, 0), rw - 1, rh - 1]
    iad1 = struct.pack(">28H", *words)
    pic_jpeg = rg.jpeg_bytes(rgb8)
    thumb_jpeg = pic_jpeg

    cd = cd if cd is not None else color_data()
    note = maker_note(cd)
    cmt1 = tiff_le([(0x010F, 2, "Canon"), (0x0110, 2, model), (0x0112, 3, orientation), (0x0132, 2, rg.META["dt"])])
    cmt2 = tiff_le([(0x829A, 5, rg.META["exp"]), (0x829D, 5, rg.META["fn"]), (0x8827, 3, rg.META["iso"])])
    timed = ctmd_sample(note) if ctmd else b""

    def build(o_jpeg, o_raw, o_ctmd):
        canon = (box(b"CNCV", b"Canon\0" * 5) + box(b"CCTP", b"\0" * 84) + box(b"CTBO", b"\0" * 84) + box(b"free", b"\0\0") +
                 box(b"CMT1", cmt1) + box(b"CMT2", cmt2) + box(b"CMT3", note) + box(b"CMT4", b"\0" * 200) + box(b"THMB", b"\0" * 44))
        uuid_canon = box(b"uuid", bytes.fromhex("85c0b687820f11e08111f4ce462b6a48") + canon)
        jpeg_trak = trak(craw_entry(w, h, box(b"JPEG", b"\0\0\0\0") + box(b"free", b"\0\0")), len(pic_jpeg), o_jpeg)
        raw_trak = trak(craw_entry(rw, rh, box(b"CMP1", bytes(cmp1)) + box(b"CDI1", b"\0\0\0\0" + box(b"IAD1", iad1))), len(sample), o_raw)
        tracks = jpeg_trak + raw_trak
        if ctmd:
            tracks += trak(box(b"CTMD", b"\0" * 6 + b"\0\1" + b"\0" * 4), len(timed), o_ctmd, b"meta")
        moov = box(b"moov", uuid_canon + fbox(b"mvhd", b"\0" * 100) + tracks)
        return box(b"ftyp", b"crx \0\0\0\1crx isom"), moov

    ftyp, moov = build(0, 0, 0)
    start = len(ftyp) + len(moov) + 16
    ftyp, moov = build(start, start + len(pic_jpeg), start + len(pic_jpeg) + len(sample))
    mdat = struct.pack(">I", 1) + b"mdat" + struct.pack(">Q", 16 + len(pic_jpeg) + len(sample) + len(timed)) + pic_jpeg + sample + timed
    open(path, "wb").write(ftyp + moov + mdat)
    return expected
