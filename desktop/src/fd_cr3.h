/* fd_cr3.h - FILM LOOK DESKTOP: the sensor data of a Canon CR3 (EOS R, RP, R5, R6 ... and the PowerShots with CRX), for the program on the
 * computer only. The camera module never sees this file.
 *
 *   1. the CRX decoder: the lossless coding and C-RAW (three levels of the integer 5/3 wavelet of JPEG 2000), version 1 of the coding
 *      (version 2 with its quantization maps is not decoded: such a file develops from its JPEG, as before),
 *   2. the container (an ISO base media file: the sample tables, the image header CMP1, the image areas IAD1, the Canon metadata and
 *      the timed metadata that holds the maker note with the white balance and the black level),
 *   3. the matrix of the colours of each camera.
 *
 * Integer arithmetic only: the same numbers in the browser and on the command line.
 *
 * THE DECODER (section 1) IS A PORT TO C of crates/rawmakase-raw/src/codec/crx.rs of RAWmakase (https://github.com/pch/rawmakase),
 * and the table of colour matrices is built from its data/raw-matrices.toml (Adobe DNG Converter's matrices). RAWmakase is
 * distributed under the MIT licence:
 *
 *   MIT License
 *   Copyright (c) 2026 RAWmakase contributors
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 *   (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 *   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The coding itself is described in Laurent Clevy's notes on the CR3 format; the prediction is that of JPEG-LS (ITU T.87), the
 * wavelet that of JPEG 2000 (ITU T.800, annex F), the step of the quantization that of H.265. The result was compared sample by
 * sample with LibRaw's decoder on real files of the EOS RP (see tests/run_tests.py).
 *
 * The rest of the file is own code of the film look project. */
#ifndef FD_CR3_H
#define FD_CR3_H

/* ===================================================================== 1. the CRX decoder */

/* a bump allocator over one block: the decoder frees by going back to a mark (the browser's heap only frees its newest block) */
typedef struct { uint8_t *base; size_t cap, used; } fcx_arena;

static void *fcx_alloc(fcx_arena *a, size_t bytes)
{
    size_t at = (a->used + 15u) & ~(size_t)15u;
    if (bytes > a->cap || at > a->cap - bytes) return 0;
    a->used = at + bytes;
    return a->base + at;
}

/* bits, most significant first; zeros after the end (fcx_overrun tells that some were used) */
typedef struct { const uint8_t *p; size_t n, pos; uint64_t cache; int bits; size_t past; } fcx_bits;

static void fcx_bits_init(fcx_bits *b, const uint8_t *p, size_t n) { b->p = p; b->n = n; b->pos = 0; b->cache = 0; b->bits = 0; b->past = 0; }
static void fcx_refill(fcx_bits *b)
{
    while (b->bits <= 56) {
        uint64_t c = 0;
        if (b->pos < b->n) c = b->p[b->pos++]; else b->past++;
        b->cache |= c << (56 - b->bits);
        b->bits += 8;
    }
}
static uint32_t fcx_peek(fcx_bits *b, int n)
{
    if (n == 0) return 0;
    if (b->bits < n) fcx_refill(b);
    return (uint32_t)(b->cache >> (64 - n));
}
static void fcx_skip(fcx_bits *b, int n)
{
    if (b->bits < n) fcx_refill(b);
    b->cache <<= n; b->bits -= n;
}
static uint32_t fcx_get(fcx_bits *b, int n) { uint32_t v = fcx_peek(b, n); fcx_skip(b, n); return v; }
static int fcx_overrun(const fcx_bits *b) { return (uint64_t)b->past * 8 > (uint64_t)b->bits; }

static int fcx_clz(uint32_t w)                      /* w != 0 */
{
    int n = 0;
    if (!(w >> 16)) { n += 16; w <<= 16; }
    if (!(w >> 24)) { n += 8; w <<= 8; }
    if (!(w >> 28)) { n += 4; w <<= 4; }
    if (!(w >> 30)) { n += 2; w <<= 2; }
    if (!(w >> 31)) n += 1;
    return n;
}

/* a Golomb-Rice code with parameter k: a unary quotient (zeros, then a one) and k low bits; 41 zeros escape a 21 bit value. -1 = damaged */
static int fcx_rice(fcx_bits *p, unsigned k, uint32_t *out)
{
    uint32_t w = fcx_peek(p, 32), zeros;
    if (w) { zeros = (uint32_t)fcx_clz(w); fcx_skip(p, (int)zeros + 1); }
    else {
        fcx_skip(p, 32); zeros = 32;
        for (;;) {
            if (fcx_overrun(p)) return -1;
            if (fcx_get(p, 1) == 1) break;
            if (++zeros > 41) return -1;
        }
    }
    if (zeros == 41) { *out = fcx_get(p, 21); return 0; }
    *out = (zeros << k) | fcx_get(p, (int)k);
    return 0;
}

static int32_t fcx_signed(uint32_t s) { return (int32_t)(s >> 1) ^ -(int32_t)(s & 1); }      /* 0, -1, 1, -2, 2 ... */

static unsigned fcx_adapt(unsigned k, uint32_t s)          /* the next Rice parameter */
{
    if (s >= (3u << k)) return s >= (3u << (k + 1)) ? (k + 2 > 20 ? 20 : k + 2) : (k + 1 > 20 ? 20 : k + 1);
    if (k > 0 && s < (1u << (k - 1))) return k - 1;
    return k;
}

static int32_t fcx_median(int32_t left, int32_t above, int32_t corner)         /* the predictor of JPEG-LS */
{
    int32_t mx = left > above ? left : above, mn = left < above ? left : above;
    if (corner >= mx) return mn;
    if (corner <= mn) return mx;
    return (int32_t)((uint32_t)left + (uint32_t)above - (uint32_t)corner);
}

static const uint8_t fcx_runj[32] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

/* the length of a run (1 .. left) in the run mode of JPEG-LS; -1 = damaged */
static int fcx_run(fcx_bits *p, int *index, int left)
{
    int n = 1;
    while (n < left) {
        int j = fcx_runj[*index];
        if (fcx_get(p, 1) == 0) {
            n += (int)fcx_get(p, j);
            if (*index > 0) (*index)--;
            return n > left ? -1 : n;
        }
        n += 1 << j;
        if (n <= left && *index < 31) (*index)++;
    }
    return n < left ? n : left;
}

/* a plane (or the low-pass band) of w x h values, coded without loss: out = w * h values */
static int fcx_lossless(fcx_arena *A, const uint8_t *src, size_t sn, int w, int h, int32_t *out)
{
    fcx_bits p;
    size_t mark = A->used;
    int32_t *zero;
    unsigned k = 0;
    int index = 0, y, x;
    if (w <= 0 || h <= 0) return SE_E_CORRUPT;
    zero = (int32_t *)fcx_alloc(A, (size_t)w * 4);
    if (!zero) return SE_E_NOMEM;
    memset(zero, 0, (size_t)w * 4);
    fcx_bits_init(&p, src, sn);
    for (y = 0; y < h; y++) {
        int32_t *cur = out + (size_t)y * (size_t)w;
        const int32_t *prev = y ? cur - w : zero;
        int32_t left = prev[0];
        x = 0;
        while (x < w) {
            int from_above = 0;
            int32_t above, corner, right, pred, v;
            uint32_t s, est;
            if (x + 1 < w && left == prev[x] && left == prev[x + 1] && fcx_get(&p, 1) == 1) {
                int n = fcx_run(&p, &index, w - x), i;
                if (n < 0) return SE_E_CORRUPT;
                for (i = 0; i < n; i++) cur[x + i] = left;
                x += n;
                if (x == w) break;
                from_above = 1;
            }
            above = prev[x];
            corner = x == 0 ? above : prev[x - 1];
            right = x + 1 < w ? prev[x + 1] : above;
            if (fcx_rice(&p, k, &s)) return SE_E_CORRUPT;
            pred = from_above ? above : fcx_median(left, above, corner);
            v = (int32_t)((uint32_t)pred + (uint32_t)fcx_signed(s));
            cur[x] = v; left = v;
            if (y > 0 && x + 1 < w) {
                int64_t d = right > above ? (int64_t)right - above : (int64_t)above - right;
                uint64_t e = ((uint64_t)s + 2u * (uint64_t)d) / 2u;
                est = e > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)e;
            } else est = s;
            k = fcx_adapt(k, est);
            x++;
        }
        if (fcx_overrun(&p)) return SE_E_CORRUPT;
    }
    A->used = mark;
    return SE_OK;
}

/* a high-pass band of w x h coefficients */
static int fcx_highpass(fcx_arena *A, const uint8_t *src, size_t sn, int w, int h, int32_t *out)
{
    fcx_bits p;
    size_t mark = A->used;
    int32_t *zero;
    uint32_t *ks, *ksp, *tmp;
    unsigned k = 0;
    int index = 0, y, x;
    if (w <= 0 || h <= 0) return SE_E_CORRUPT;
    zero = (int32_t *)fcx_alloc(A, (size_t)w * 4);
    ks = (uint32_t *)fcx_alloc(A, (size_t)w * 4);
    ksp = (uint32_t *)fcx_alloc(A, (size_t)w * 4);
    if (!zero || !ks || !ksp) return SE_E_NOMEM;
    memset(zero, 0, (size_t)w * 4); memset(ks, 0, (size_t)w * 4); memset(ksp, 0, (size_t)w * 4);
    fcx_bits_init(&p, src, sn);
    for (y = 0; y < h; y++) {
        int32_t *cur = out + (size_t)y * (size_t)w;
        const int32_t *prev = y ? cur - w : zero;
        int32_t left = 0;
        x = 0;
        while (x < w) {
            int nonzero = 0;
            uint32_t s;
            int32_t v;
            if (x + 1 < w && left == 0 && prev[x] == 0 && prev[x + 1] == 0) {
                if (fcx_get(&p, 1) == 1) {
                    int n = fcx_run(&p, &index, w - x), i;
                    if (n < 0) return SE_E_CORRUPT;
                    for (i = 0; i < n; i++) { cur[x + i] = 0; ks[x + i] = 0; }
                    x += n;
                    if (x == w) break;
                }
                nonzero = 1;                                  /* after a run, or instead of one, the coefficient is not 0 */
            }
            if (fcx_rice(&p, k, &s)) return SE_E_CORRUPT;
            v = fcx_signed(s > 0xFFFFFFFEu ? s : s + (uint32_t)nonzero);
            cur[x] = v; left = v;
            k = fcx_adapt(k, s);
            if (y > 0 && x + 1 < w && ksp[x + 1] > k + 1) k++;     /* a bigger parameter above and to the right raises it by one */
            ks[x] = k;
            x++;
        }
        if (fcx_overrun(&p)) return SE_E_CORRUPT;
        tmp = ks; ks = ksp; ksp = tmp;
    }
    A->used = mark;
    return SE_OK;
}

/* the lengths of one level of a line of n values split in two: the low-pass band as it is coded, the high-pass band and the length that is split
 * next. A neighbour before the line adds one high-pass value, a neighbour after it makes the line odd (see the notes of RAWmakase). */
static int fcx_split(int n, int before, int after, int coarsest, int *low, int *high, int *next)
{
    if (!before && !after) { *low = (n + 1) / 2; *high = n / 2; }
    else if (before && !after) { *low = (n + 1) / 2; *high = n / 2 + 1; }
    else if (!before && after) { int half = ((n | 1) + 1) / 2; *low = coarsest ? half : (half | 1); *high = half; }
    else return -1;                                           /* a tile between two neighbours */
    *next = *low;
    return 0;
}

static int32_t fcx_sar(int32_t v, int n) { return v >> n; }

/* the inverse 5/3 lifting of one line from its low and high-pass halves; with a neighbour before it the high-pass half starts one value early,
 * with one after it both halves run one value on; without a neighbour the line is extended symmetrically */
static void fcx_line(const int32_t *low, int nlow, const int32_t *high, int nhigh, int32_t *out, int n, int before, int after)
{
    int i, b = before ? 1 : 0, half = n / 2, even = (n + 1) / 2;
    if (nhigh == 0) { if (n > 0 && nlow > 0) out[0] = low[0]; return; }
#define FCX_HI(i) high[(i) < nhigh ? (i) : nhigh - 1]
#define FCX_EVEN(i, res) do { int32_t l_ = FCX_HI((i) + b > 0 ? (i) + b - 1 : 0), r_ = FCX_HI((i) + b); \
        (res) = (int32_t)((uint32_t)low[(i) < nlow ? (i) : nlow - 1] - (uint32_t)fcx_sar((int32_t)((uint32_t)l_ + (uint32_t)r_ + 2u), 2)); } while (0)
    for (i = 0; i < even; i++) { int32_t e; FCX_EVEN(i, e); out[2 * i] = e; }
    for (i = 0; i < half; i++) {
        int32_t l = out[2 * i], r;
        if (2 * i + 2 < n) r = out[2 * i + 2];
        else if (after) { FCX_EVEN(i + 1, r); }
        else r = l;
        out[2 * i + 1] = (int32_t)((uint32_t)FCX_HI(i + b) + (uint32_t)fcx_sar((int32_t)((uint32_t)l + (uint32_t)r), 1));
    }
#undef FCX_EVEN
#undef FCX_HI
}

/* one level of the inverse wavelet, rows first, then columns (in place): the four bands (low-pass, horizontal, vertical and diagonal high-pass) with
 * their widths, the heights of the upper and the lower pair, the size of the result w x h, written to dst (w * h values) */
static int fcx_synth(fcx_arena *A, const int32_t *ll, const int32_t *hl, const int32_t *lh, const int32_t *hh, int low, int high, int top, int bottom,
                     int w, int h, int before, int after, int32_t *dst)
{
    size_t mark = A->used;
    int32_t *lo, *hi, *col;
    int x, y;
    if (top + bottom != h) return SE_E_CORRUPT;
    for (y = 0; y < h; y++) {
        int32_t *o = dst + (size_t)y * (size_t)w;
        if (y < top) fcx_line(ll + (size_t)y * (size_t)low, low, hl + (size_t)y * (size_t)high, high, o, w, before, after);
        else fcx_line(lh + (size_t)(y - top) * (size_t)low, low, hh + (size_t)(y - top) * (size_t)high, high, o, w, before, after);
    }
    lo = (int32_t *)fcx_alloc(A, (size_t)(top + 1) * 4);
    hi = (int32_t *)fcx_alloc(A, (size_t)(bottom + 1) * 4);
    col = (int32_t *)fcx_alloc(A, (size_t)(h + 1) * 4);
    if (!lo || !hi || !col) return SE_E_NOMEM;
    for (x = 0; x < w; x++) {
        for (y = 0; y < top; y++) lo[y] = dst[(size_t)y * (size_t)w + (size_t)x];
        for (y = 0; y < bottom; y++) hi[y] = dst[(size_t)(y + top) * (size_t)w + (size_t)x];
        fcx_line(lo, top, hi, bottom, col, h, 0, 0);
        for (y = 0; y < h; y++) dst[(size_t)y * (size_t)w + (size_t)x] = col[y];
    }
    A->used = mark;
    return SE_OK;
}

/* the step of the quantization of parameter q: H.265's scale, doubled every six steps, in 64ths */
static int32_t fcx_step(uint32_t q)
{
    static const int32_t sc[6] = { 40, 45, 51, 57, 64, 72 };
    if (q / 6 > 24) return 0x7FFFFFFF;
    return (sc[q % 6] << (q / 6)) / 64;
}

static void fcx_scale(int32_t *v, size_t n, int32_t step)
{
    size_t i;
    if (step <= 1) return;
    for (i = 0; i < n; i++) {
        int64_t m = (int64_t)v[i] * step;
        v[i] = m > 0x7FFFFFFF ? 0x7FFFFFFF : (m < -0x7FFFFFFF - 1 ? -0x7FFFFFFF - 1 : (int32_t)m);
    }
}

typedef struct { size_t start, end; uint32_t q; } fcx_band;
typedef struct { int left, top, w, h; fcx_band band[4][10]; } fcx_tile;

/* a C-RAW plane of w x h values: the bands decoded, scaled back and put together. plane = w * h values */
static int fcx_wavelet(fcx_arena *A, const uint8_t *sample, const fcx_band *bands, int w, int h, int before, int after, int32_t *plane)
{
    int ln[3], lm[3], llow[3], lhigh[3], ltop[3], lbot[3];
    int n = w, m = h, level, rc;
    size_t base, mark = A->used;
    int32_t *ll;
    for (level = 0; level < 3; level++) {
        int low, high, next;
        if (fcx_split(n, before, after, level == 2, &low, &high, &next)) return FR_NONE;
        ln[level] = n; lm[level] = m; llow[level] = low; lhigh[level] = high; ltop[level] = (m + 1) / 2; lbot[level] = m / 2;
        n = next; m = (m + 1) / 2;
    }
    if (n <= 0 || m <= 0) return SE_E_CORRUPT;
    ll = (int32_t *)fcx_alloc(A, (size_t)n * (size_t)m * 4);
    if (!ll) return SE_E_NOMEM;
    base = (size_t)((uint8_t *)ll - A->base);                       /* where the low-pass band is: the results of the levels take its place */
    rc = fcx_lossless(A, sample + bands[0].start, bands[0].end - bands[0].start, n, m, ll);
    if (rc) return rc;
    fcx_scale(ll, (size_t)n * (size_t)m, fcx_step(bands[0].q) < 1 ? 1 : fcx_step(bands[0].q));
    for (level = 2; level >= 0; level--) {
        int ow = ln[level], oh = lm[level], low = llow[level], high = lhigh[level], top = ltop[level], bottom = lbot[level];
        int i = 1 + 3 * (2 - level), c;
        int32_t *hlb, *lhb, *hhb, *dst;
        hlb = (int32_t *)fcx_alloc(A, (size_t)high * (size_t)top * 4);
        lhb = (int32_t *)fcx_alloc(A, (size_t)low * (size_t)bottom * 4);
        hhb = (int32_t *)fcx_alloc(A, (size_t)high * (size_t)bottom * 4);
        if (!hlb || !lhb || !hhb) return SE_E_NOMEM;
        rc = fcx_highpass(A, sample + bands[i].start, bands[i].end - bands[i].start, high, top, hlb);
        if (!rc) rc = fcx_highpass(A, sample + bands[i + 1].start, bands[i + 1].end - bands[i + 1].start, low, bottom, lhb);
        if (!rc) rc = fcx_highpass(A, sample + bands[i + 2].start, bands[i + 2].end - bands[i + 2].start, high, bottom, hhb);
        if (rc) return rc;
        for (c = 0; c < 3; c++) {
            int32_t st = fcx_step(bands[i + c].q);
            if (st < 1) st = 1;
            if (c == 0) fcx_scale(hlb, (size_t)high * (size_t)top, st);
            else if (c == 1) fcx_scale(lhb, (size_t)low * (size_t)bottom, st);
            else fcx_scale(hhb, (size_t)high * (size_t)bottom, st);
        }
        dst = level == 0 ? plane : (int32_t *)fcx_alloc(A, (size_t)ow * (size_t)oh * 4);
        if (!dst) return SE_E_NOMEM;
        rc = fcx_synth(A, ll, hlb, lhb, hhb, low, high, top, bottom, ow, oh, before, after, dst);
        if (rc) return rc;
        if (level > 0) {                                      /* the result takes the place of the bands it was made of */
            memmove(A->base + base, dst, (size_t)ow * (size_t)oh * 4);
            ll = (int32_t *)(A->base + base);
            A->used = base + (size_t)ow * (size_t)oh * 4;
        }
    }
    A->used = mark;
    return SE_OK;
}

static uint32_t fcx_be(const uint8_t *b, size_t n, size_t at, int len, int *bad)
{
    uint32_t v = 0;
    int i;
    if (at > n || (size_t)len > n - at) { *bad = 1; return 0; }
    for (i = 0; i < len; i++) v = (v << 8) | b[at + (size_t)i];
    return v;
}

/* the tiles, planes and bands that the tile headers describe (at the start of the sample). count = the number of tiles */
static int fcx_layout(const fcx_hdr *h, const uint8_t *sample, size_t sn, fcx_tile *tl, int count, int across)
{
    size_t at = 0, data = h->header_size;
    int t, p, b, bands = 1 + 3 * h->levels, bad = 0;
    size_t hs = h->header_size;
    if (hs > sn) return SE_E_CORRUPT;
    for (t = 0; t < count; t++) {
        size_t body, len, tile_end, plane_end;
        uint32_t m = fcx_be(sample, hs, at, 2, &bad), size;
        len = fcx_be(sample, hs, at + 2, 2, &bad);
        if (bad || m != 0xFF01u) return SE_E_CORRUPT;
        body = at + 4; at = body + len;
        if (at > hs || len < 8) return SE_E_CORRUPT;
        size = fcx_be(sample, hs, body, 4, &bad);
        if (bad || size > sn - data) return SE_E_CORRUPT;
        tile_end = data + size;
        for (p = 0; p < h->planes; p++) {
            uint32_t psize;
            m = fcx_be(sample, hs, at, 2, &bad); len = fcx_be(sample, hs, at + 2, 2, &bad);
            if (bad || m != 0xFF02u) return SE_E_CORRUPT;
            body = at + 4; at = body + len;
            if (at > hs) return SE_E_CORRUPT;
            psize = fcx_be(sample, hs, body, 4, &bad);
            if (bad || psize > tile_end - data) return SE_E_CORRUPT;
            plane_end = data + psize;
            for (b = 0; b < bands; b++) {
                uint32_t bsize, flags;
                m = fcx_be(sample, hs, at, 2, &bad); len = fcx_be(sample, hs, at + 2, 2, &bad);
                if (bad || m != 0xFF03u) return SE_E_CORRUPT;
                body = at + 4; at = body + len;
                if (at > hs) return SE_E_CORRUPT;
                bsize = fcx_be(sample, hs, body, 4, &bad); flags = fcx_be(sample, hs, body + 4, 4, &bad);
                if (bad || bsize > plane_end - data) return SE_E_CORRUPT;
                tl[t].band[p][b].start = data; tl[t].band[p][b].end = data + bsize; tl[t].band[p][b].q = (flags >> 19) & 0xFFu;
                data += bsize;
            }
            data = plane_end;
        }
        data = tile_end;
        tl[t].left = (t % across) * h->tile_w; tl[t].top = (t / across) * h->tile_h;
        tl[t].w = h->tile_w < h->width - tl[t].left ? h->tile_w : h->width - tl[t].left;
        tl[t].h = h->tile_h < h->height - tl[t].top ? h->tile_h : h->height - tl[t].top;
    }
    return SE_OK;
}

/* decodes the sensor data of a CR3 into out (width x height values of the mosaic). SE_OK, FR_NONE (a coding that is not decoded here) or an error */
static int fcx_decode(const fcx_hdr *h, const uint8_t *sample, size_t sn, uint16_t *out)
{
    int across, down, count, t, p, rc = SE_OK, mid, maxv;
    size_t pmax, cap;
    fcx_tile *tl;
    fcx_arena A;
    int32_t *plane;
    if (h->version != 0x100 || h->planes != 4 || h->encoding != 0 || (h->levels != 0 && h->levels != 3) || h->cfa < 0 || h->cfa > 3 || h->bits < 8 || h->bits > 16) return FR_NONE;
    if (h->width <= 0 || h->height <= 0 || h->tile_w <= 0 || h->tile_h <= 0 || (h->width & 1) || (h->height & 1) || (h->tile_w & 1) || (h->tile_h & 1)) return SE_E_CORRUPT;
    if (h->width > 30000 || h->height > 30000 || (int64_t)h->width * h->height > 150000000 || h->header_size > (1u << 20)) return FR_NONE;
    across = (h->width + h->tile_w - 1) / h->tile_w; down = (h->height + h->tile_h - 1) / h->tile_h;
    count = across * down;
    if (count > 1024) return FR_NONE;
    if (h->levels > 0 && h->tile_h < h->height) return FR_NONE;                    /* C-RAW tiles stacked vertically */
    tl = (fcx_tile *)malloc((size_t)count * sizeof(fcx_tile));
    if (!tl) return SE_E_NOMEM;
    rc = fcx_layout(h, sample, sn, tl, count, across);
    if (rc) { free(tl); return rc; }
    {   /* the biggest plane of a tile, and the memory the decoding of it needs */
        int pw = (h->tile_w < h->width ? h->tile_w : h->width) / 2, ph = (h->tile_h < h->height ? h->tile_h : h->height) / 2;
        pmax = (size_t)pw * (size_t)ph;
        cap = (pmax * 2 + (size_t)(pw + ph) * 16 + 65536u) * 4;
    }
    A.cap = cap; A.used = 0;
    A.base = (uint8_t *)malloc(cap);
    if (!A.base) { free(tl); return SE_E_NOMEM; }
    plane = (int32_t *)fcx_alloc(&A, pmax * 4);
    mid = 1 << (h->bits - 1); maxv = (1 << h->bits) - 1;
    for (t = 0; t < count && !rc; t++) {
        const fcx_tile *tt = &tl[t];
        int pw = tt->w / 2, ph = tt->h / 2, before = tt->left > 0, after = tt->left + tt->w < h->width, j, i;
        if ((size_t)pw * (size_t)ph > pmax) { rc = SE_E_CORRUPT; break; }
        for (p = 0; p < 4 && !rc; p++) {
            size_t mark = A.used;
            const fcx_band *bd = tt->band[p];
            if (h->levels == 0) rc = fcx_lossless(&A, sample + bd[0].start, bd[0].end - bd[0].start, pw, ph, plane);
            else rc = fcx_wavelet(&A, sample, bd, pw, ph, before, after, plane);
            A.used = mark;
            if (rc) break;
            for (j = 0; j < ph; j++) {
                const int32_t *row = plane + (size_t)j * (size_t)pw;
                uint16_t *line = out + (size_t)(tt->top + 2 * j + p / 2) * (size_t)h->width + (size_t)tt->left + (size_t)(p % 2);
                for (i = 0; i < pw; i++) {
                    int64_t v = (int64_t)row[i] + mid;
                    line[2 * i] = (uint16_t)(v < 0 ? 0 : (v > maxv ? maxv : v));
                }
            }
        }
    }
    free(A.base); free(tl);
    return rc;
}

/* ===================================================================== 2. the container */
/* the matrices XYZ (D65) -> camera, the way Adobe DNG Converter writes them (ten-thousandths), from RAWmakase's data/raw-matrices.toml */
typedef struct { const char *model; int m[9]; } fcx_cam;
static const fcx_cam fcx_cams[] = {
    { "EOS R", { 8293, -1789, -1094, -5025, 12925, 2327, -1199, 2769, 6108 } },
    { "EOS RP", { 8608, -2097, -1178, -5425, 13265, 2383, -1149, 2238, 5680 } },
    { "EOS R3", { 9423, -2839, -1195, -4532, 12377, 2415, -483, 1374, 5276 } },
    { "EOS R5", { 9766, -2953, -1254, -4276, 12116, 2433, -437, 1336, 5131 } },
    { "EOS R5 Mark II", { 9396, -2598, -1207, -4408, 12296, 2369, -505, 1575, 6077 } },
    { "EOS R6", { 8293, -1611, -1132, -4759, 12711, 2275, -1013, 2415, 5509 } },
    { "EOS R6 Mark II", { 9539, -2795, -1224, -4175, 11998, 2458, -465, 1755, 6048 } },
    { "EOS R6 Mark III", { 8309, -1786, -1095, -4851, 12736, 2353, -956, 1911, 5534 } },
    { "EOS R7", { 10424, -3138, -1300, -4221, 11938, 2584, -547, 1658, 6183 } },
    { "EOS R8", { 9539, -2795, -1224, -4175, 11998, 2458, -465, 1755, 6048 } },
    { "EOS R8 Mark II", { 9539, -2795, -1224, -4175, 11998, 2458, -465, 1755, 6048 } },
    { "EOS R10", { 9269, -2012, -1107, -3990, 11762, 2527, -569, 2093, 4913 } },
    { "EOS R50", { 9269, -2012, -1107, -3990, 11762, 2527, -569, 2093, 4913 } },
    { "EOS R50 V", { 9089, -1810, -1209, -4294, 12116, 2454, -498, 1862, 4923 } },
    { "EOS R100", { 8230, -1515, -1032, -4179, 12005, 2454, -649, 2076, 4711 } },
    { "EOS M50", { 8532, -701, -1167, -4095, 11879, 2508, -797, 2424, 7010 } },
    { "EOS M50 Mark II", { 10463, -2173, -1437, -4856, 12635, 2482, -1216, 2915, 7237 } },
    { "EOS M200", { 10463, -2173, -1437, -4856, 12635, 2482, -1216, 2915, 7237 } },
    { "EOS M6 Mark II", { 11498, -3759, -1516, -5073, 12954, 2349, -892, 1867, 6118 } },
    { "EOS 90D", { 11498, -3759, -1516, -5073, 12954, 2349, -892, 1867, 6118 } },
    { "EOS 250D", { 9079, -1923, -1236, -4677, 12454, 2492, -922, 2319, 5565 } },
    { "EOS 850D", { 9079, -1923, -1236, -4677, 12454, 2492, -922, 2319, 5565 } },
    { "EOS-1D X Mark III", { 8971, -2022, -1242, -5405, 13249, 2380, -1280, 2483, 6072 } },
    { "PowerShot G5 X Mark II", { 11629, -5713, -914, -2706, 11090, 1842, -206, 1225, 5515 } },
    { "PowerShot G7 X Mark III", { 11629, -5713, -914, -2706, 11090, 1842, -206, 1225, 5515 } },
    { "PowerShot SX70 HS", { 18285, -8907, -1951, -1845, 10688, 1323, 364, 1101, 5139 } },
};

/* the model as the file writes it ("Canon EOS RP") -> its row in the table, or -1. Some regions and cameras write another name */
static int fcx_cam_find(const char *model)
{
    static const char *const alias[][2] = {
        { "EOS KISS M", "EOS M50" }, { "EOS Rebel SL3", "EOS 250D" }, { "EOS 200D II", "EOS 250D" }, { "EOS M50m2", "EOS M50 Mark II" },
        { "EOS R6m2", "EOS R6 Mark II" }, { "EOS R5m2", "EOS R5 Mark II" }
    };
    unsigned i;
    const char *s = model;
    if (strncmp(s, "Canon ", 6) == 0) s += 6;
    for (i = 0; i < sizeof alias / sizeof alias[0]; i++) if (strcmp(s, alias[i][0]) == 0) { s = alias[i][1]; break; }
    for (i = 0; i < sizeof fcx_cams / sizeof fcx_cams[0]; i++) if (strcmp(s, fcx_cams[i].model) == 0) return (int)i;
    return -1;
}

typedef struct {
    int craw;                          /* the sample entry is a CRAW (a raw image) */
    int w, h;                          /* of the entry */
    int have_cmp1, have_iad1;
    size_t cmp1_s, cmp1_e, iad1_s, iad1_e;
    int have_sample;
    uint64_t soff, ssize;              /* the first sample of the track */
    uint32_t entry;                    /* the four letters of the sample entry */
} fcx_track;

static uint32_t fcx_u16(const uint8_t *f, size_t at) { return ((uint32_t)f[at] << 8) | f[at + 1]; }

/* one 'trak' box: its sample entry and where its first sample is */
static void fcx_trak(const uint8_t *f, size_t n, size_t s, size_t e, fcx_track *t)
{
    size_t ms, me, is, ie, ss, se, a, b, es, ee, pos;
    uint32_t type;
    uint64_t size = 0, off = 0;
    int have_off = 0;
    memset(t, 0, sizeof *t);
    if (!fc_child(f, s, e, FC4('m', 'd', 'i', 'a'), &ms, &me) || !fc_child(f, ms, me, FC4('m', 'i', 'n', 'f'), &is, &ie) ||
        !fc_child(f, is, ie, FC4('s', 't', 'b', 'l'), &ss, &se)) return;
    if (fc_child(f, ss, se, FC4('s', 't', 's', 'd'), &a, &b) && b - a >= 8) {
        pos = a + 8;                                                  /* version and flags, the number of entries */
        if (fc_next(f, b, &pos, &type, &es, &ee)) {
            t->entry = type;
            if (type == FC4('C', 'R', 'A', 'W') && ee - es >= 82) {
                size_t cp = es + 82, cs, ce;
                uint32_t ct;
                t->craw = 1; t->w = (int)fcx_u16(f, es + 24); t->h = (int)fcx_u16(f, es + 26);
                while (fc_next(f, ee, &cp, &ct, &cs, &ce)) {
                    if (ct == FC4('C', 'M', 'P', '1')) { t->have_cmp1 = 1; t->cmp1_s = cs; t->cmp1_e = ce; }
                    else if (ct == FC4('C', 'D', 'I', '1') && ce - cs >= 4) {
                        size_t ap = cs + 4, as, ae;
                        uint32_t at;
                        while (fc_next(f, ce, &ap, &at, &as, &ae)) if (at == FC4('I', 'A', 'D', '1')) { t->have_iad1 = 1; t->iad1_s = as; t->iad1_e = ae; }
                    }
                }
            }
        }
    }
    if (fc_child(f, ss, se, FC4('s', 't', 's', 'z'), &a, &b) && b - a >= 12) {
        uint32_t cnt = fc_be32(f + a + 8);
        size = fc_be32(f + a + 4);
        if (cnt > 0 && size == 0 && b - a >= 16) size = fc_be32(f + a + 12);
        if (cnt == 0) size = 0;
    }
    if (fc_child(f, ss, se, FC4('c', 'o', '6', '4'), &a, &b) && b - a >= 16 && fc_be32(f + a + 4) > 0) {
        off = ((uint64_t)fc_be32(f + a + 8) << 32) | fc_be32(f + a + 12); have_off = 1;
    } else if (fc_child(f, ss, se, FC4('s', 't', 'c', 'o'), &a, &b) && b - a >= 12 && fc_be32(f + a + 4) > 0) {
        off = fc_be32(f + a + 8); have_off = 1;
    }
    if (have_off && size > 0 && off < n && size <= n - off) { t->have_sample = 1; t->soff = off; t->ssize = size; }
}

/* the black level and the white of the maker note's ColorData, by version of the record: the places of the as shot white balance (red, green, green,
 * blue), the per channel black level, the specular white level; 0 = this version is not known */
typedef struct { int version_lo, version_hi, wb, black, white; } fcx_cd;
static const fcx_cd fcx_cds[] = {
    { 16, 19, 71, 329, 797 }, { 32, 33, 85, 343, 811 }, { 34, 34, 105, 363, 641 }, { 48, 48, 105, 363, 641 },
    { 64, 64, 105, 383, 661 }, { 65, 66, 105, 383, 661 }, { 67, 67, 105, 383, 661 }
};

/* the maker note (a TIFF stream) in the records of the timed metadata: 1 = found and read */
static int fcx_note_colors(const uint8_t *note, size_t nn, int wb[4], int black[4], int *white, int *have_black, int *have_white)
{
    fr_tf t;
    fr_ent cd;
    size_t ifd;
    int ver, i, found = 0;
    t.p = note; t.n = nn; t.be = nn >= 2 && note[0] == 'M' && note[1] == 'M';
    if (nn < 16) return 0;
    ifd = fr_u32(&t, 4);
    cd = fr_find(&t, ifd, 0x4001);
    if (!cd.ok || cd.type != 3 || cd.cnt < 100) return 0;
    ver = (int)fr_val(&t, &cd, 0);
    for (i = 0; i < (int)(sizeof fcx_cds / sizeof fcx_cds[0]); i++) {
        const fcx_cd *c = &fcx_cds[i];
        int k;
        if (ver < c->version_lo || ver > c->version_hi) continue;
        if ((uint32_t)(c->wb + 4) > cd.cnt) continue;
        for (k = 0; k < 4; k++) wb[k] = (int)fr_val(&t, &cd, (uint32_t)(c->wb + k));
        found = 1;
        if ((uint32_t)(c->black + 4) <= cd.cnt) { for (k = 0; k < 4; k++) black[k] = (int)fr_val(&t, &cd, (uint32_t)(c->black + k)); *have_black = 1; }
        if ((uint32_t)(c->white + 1) <= cd.cnt) { *white = (int)fr_val(&t, &cd, (uint32_t)c->white); *have_white = 1; }
        break;
    }
    return found;
}

/* the records of the timed metadata sample (types 7 to 9: little endian lengths, items of a length, a tag and a TIFF stream; the tag 0x927c is
 * the maker note) */
static int fcx_timed_colors(const uint8_t *s, size_t n, int wb[4], int black[4], int *white, int *have_black, int *have_white)
{
    size_t at = 0;
    int guard = 0;
    while (at + 4 <= n && guard++ < 4096) {
        size_t len = (size_t)s[at] | ((size_t)s[at + 1] << 8) | ((size_t)s[at + 2] << 16) | ((size_t)s[at + 3] << 24), i;
        unsigned kind;
        if (len < 12 || len > n - at) break;
        kind = (unsigned)s[at + 4] | ((unsigned)s[at + 5] << 8);
        if (kind >= 7 && kind <= 9) {
            i = 12;
            while (i + 8 <= len) {
                size_t il = (size_t)s[at + i] | ((size_t)s[at + i + 1] << 8) | ((size_t)s[at + i + 2] << 16) | ((size_t)s[at + i + 3] << 24);
                uint32_t tag;
                if (il < 8 || il > len - i) break;
                tag = (uint32_t)s[at + i + 4] | ((uint32_t)s[at + i + 5] << 8) | ((uint32_t)s[at + i + 6] << 16) | ((uint32_t)s[at + i + 7] << 24);
                if (tag == 0x927cu && fcx_note_colors(s + at + i + 8, il - 8, wb, black, white, have_black, have_white)) return 1;
                i += il;
            }
        }
        at += len;
    }
    return 0;
}

static void fcx_copy_text(char *dst, int cap, const fr_tf *t, const fr_ent *e) { fr_copy_ascii(dst, cap, t, e); }

/* a CR3: what the program needs to know about its sensor data. SE_OK, FR_NONE (nothing usable: the JPEG is taken) or an error */
static int fr_parse_cr3(const uint8_t *s, size_t n, fr_info *ri)
{
    size_t pos = 0, a, b, ms = 0, me = 0, cmt1_s = 0, cmt1_e = 0, cmt3_s = 0, cmt3_e = 0;
    uint32_t type;
    fcx_track tr[16], ctmd_tr;
    int ntr = 0, best = -1, i, have_ctmd = 0;
    fcx_hdr *H = &ri->crx;
    fr_tf t;
    fr_ent e;
    size_t ifd0;
    int cam, left, top, right, bottom, wb[4] = { 0, 0, 0, 0 }, blk[4] = { 0, 0, 0, 0 }, white = 0, have_black = 0, have_white = 0, have_wb = 0;
    int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0, have_crop = 0, have_act = 0, redrow, ph;
    int cfa_base[4];
    memset(ri, 0, sizeof *ri);
    if (n < 64 || fc_be32(s + 4) != FC4('f', 't', 'y', 'p') || fc_be32(s + 8) != FC4('c', 'r', 'x', ' ')) return SE_E_BADFILE;
    while (fc_next(s, n, &pos, &type, &a, &b)) if (type == FC4('m', 'o', 'o', 'v')) { ms = a; me = b; break; }
    if (!ms) return SE_E_BADFILE;
    memset(&ctmd_tr, 0, sizeof ctmd_tr);
    pos = ms;
    while (fc_next(s, me, &pos, &type, &a, &b)) {
        if (type == FC4('u', 'u', 'i', 'd') && b - a > 16 && fc_same(s + a, fc_uuid_canon, 16)) {
            size_t c = a + 16, cs, ce;
            uint32_t ct;
            while (fc_next(s, b, &c, &ct, &cs, &ce)) {
                if (ct == FC4('C', 'M', 'T', '1')) { cmt1_s = cs; cmt1_e = ce; }
                else if (ct == FC4('C', 'M', 'T', '3')) { cmt3_s = cs; cmt3_e = ce; }
            }
        } else if (type == FC4('t', 'r', 'a', 'k') && ntr < 16) {
            fcx_trak(s, n, a, b, &tr[ntr]);
            if (tr[ntr].entry == FC4('C', 'T', 'M', 'D') && tr[ntr].have_sample && !have_ctmd) { ctmd_tr = tr[ntr]; have_ctmd = 1; }
            ntr++;
        }
    }
    for (i = 0; i < ntr; i++) {                                       /* the full raw image: the first of the largest raw tracks */
        if (!tr[i].craw || !tr[i].have_cmp1 || !tr[i].have_sample) continue;
        if (best < 0 || (int64_t)tr[i].w * tr[i].h > (int64_t)tr[best].w * tr[best].h) best = i;
    }
    if (best < 0 || !cmt1_s) return FR_NONE;
    if (tr[best].cmp1_e - tr[best].cmp1_s < 32) return FR_NONE;                  /* not an image header that this program knows */
    {
        const uint8_t *c = s + tr[best].cmp1_s;
        H->version = (int)((c[4] << 8) | c[5]);
        H->width = (int)fc_be32(c + 8); H->height = (int)fc_be32(c + 12); H->tile_w = (int)fc_be32(c + 16); H->tile_h = (int)fc_be32(c + 20);
        H->bits = c[24]; H->planes = c[25] >> 4; H->cfa = c[25] & 15; H->encoding = c[26] >> 4; H->levels = c[26] & 15; H->header_size = fc_be32(c + 28);
    }
    ri->off = (size_t)tr[best].soff; ri->len = (size_t)tr[best].ssize;
    if (H->version != 0x100 || H->planes != 4 || H->encoding != 0 || (H->levels != 0 && H->levels != 3) || H->cfa > 3 || H->bits < 8 || H->bits > 16) return FR_NONE;
    if (H->width < 16 || H->height < 16 || H->width > 30000 || H->height > 30000) return FR_NONE;

    t.p = s + cmt1_s; t.n = cmt1_e - cmt1_s; t.be = t.n >= 2 && t.p[0] == 'M' && t.p[1] == 'M';
    ifd0 = fr_u32(&t, 4);
    e = fr_find(&t, ifd0, 0x010f); fcx_copy_text(ri->make, sizeof ri->make, &t, &e);
    e = fr_find(&t, ifd0, 0x0110); fcx_copy_text(ri->model, sizeof ri->model, &t, &e);
    e = fr_find(&t, ifd0, 0x0112); ri->orient = (int)fr_val(&t, &e, 0);
    cam = fcx_cam_find(ri->model);
    if (cam < 0) return FR_NONE;                                      /* no colour matrix of this camera: the JPEG route */

    /* the areas of the image: the default crop (the size of the camera's JPEG) at words 8..11, the active area at words 20..23 (inclusive corners) */
    if (tr[best].have_iad1) {
        const uint8_t *ar = s + tr[best].iad1_s;
        size_t an = tr[best].iad1_e - tr[best].iad1_s;
        if (an >= 24) {
            ax0 = (int)fcx_u16(ar, 16); ay0 = (int)fcx_u16(ar, 18); ax1 = (int)fcx_u16(ar, 20); ay1 = (int)fcx_u16(ar, 22);
            have_crop = ax1 >= ax0 && ay1 >= ay0 && ax1 < H->width && ay1 < H->height;
        }
        if (an >= 48) {
            left = (int)fcx_u16(ar, 40); top = (int)fcx_u16(ar, 42); right = (int)fcx_u16(ar, 44); bottom = (int)fcx_u16(ar, 46);
            have_act = right >= left && bottom >= top && right < H->width && bottom < H->height;
            if (have_act && !have_crop) { ax0 = left; ay0 = top; ax1 = right; ay1 = bottom; have_crop = 1; }
        }
    }
    (void)have_act;
    if (have_crop) { ri->x0 = ax0; ri->y0 = ay0; ri->w = ax1 - ax0 + 1; ri->h = ay1 - ay0 + 1; }
    else { ri->x0 = 0; ri->y0 = 0; ri->w = H->width; ri->h = H->height; }
    ri->rw = H->width; ri->rh = H->height;
    ri->ncomp = 1;
    /* the mosaic: the site of red in the Bayer cell at the corner of the raw array (0 RGGB, 1 GRBG, 2 GBRG, 3 BGGR), then at the corner of the picture */
    {
        static const int pat[4][4] = { { 0, 1, 1, 2 }, { 1, 0, 2, 1 }, { 1, 2, 0, 1 }, { 2, 1, 1, 0 } };
        int p;
        for (p = 0; p < 4; p++) cfa_base[p] = pat[H->cfa][p];
        for (p = 0; p < 4; p++) ri->cfa[p] = cfa_base[(((ri->y0 + (p >> 1)) & 1) << 1) | ((ri->x0 + (p & 1)) & 1)];
        redrow = (H->cfa == 2 || H->cfa == 3) ? 1 : 0;
    }
    ph = 0; (void)ph;

    /* the white balance, the black level, the white: the maker note of the timed metadata (else the one of the Canon metadata box) */
    if (have_ctmd) have_wb = fcx_timed_colors(s + ctmd_tr.soff, (size_t)ctmd_tr.ssize, wb, blk, &white, &have_black, &have_white);
    if (!have_wb && cmt3_s) have_wb = fcx_note_colors(s + cmt3_s, cmt3_e - cmt3_s, wb, blk, &white, &have_black, &have_white);
    if (!have_wb || wb[0] <= 0 || wb[3] <= 0 || wb[1] + wb[2] <= 0) return FR_NONE;
    {
        int g = wb[1] + wb[2];
        ri->wb[0] = (int)(((int64_t)wb[0] * 2 * 65536) / g); ri->wb[1] = 65536; ri->wb[2] = (int)(((int64_t)wb[3] * 2 * 65536) / g);
    }
    {
        int range = (1 << H->bits) - 1, p;
        if (have_black) {                                              /* the levels come as red, green (the red's row), green (the other), blue */
            for (p = 0; p < 4; p++) {
                int c = ri->cfa[p], v;
                if (c == 0) v = blk[0]; else if (c == 2) v = blk[3];
                else {                                                  /* a green: the one in the row of red is the first */
                    int row_of_p = ((ri->y0 + (p >> 1)) & 1);
                    v = row_of_p == redrow ? blk[1] : blk[2];
                }
                if (v < 0 || v > range / 2) { have_black = 0; break; }
                ri->black[p] = v;
            }
        }
        if (!have_black) for (p = 0; p < 4; p++) ri->black[p] = -1;     /* measured from the masked pixels */
        ri->white = (have_white && white > 0 && white <= range && white > 1024) ? white : range;
    }
    memset(&e, 0, sizeof e);
    {
        double m[9];
        int k;
        for (k = 0; k < 9; k++) m[k] = (double)fcx_cams[cam].m[k] / 10000.0;
        if (fr_cam_matrix(m, ri->cam)) return SE_E_CORRUPT;
    }
    ri->kind = 3;
    return SE_OK;
}

static int fr_decode_cr3(const uint8_t *s, size_t n, const fr_info *ri, uint16_t *raw)
{
    if (ri->off >= n || ri->len > n - ri->off) return SE_E_TRUNCATED;
    return fcx_decode(&ri->crx, s + ri->off, ri->len, raw);
}

#endif
