/* fd_raw.h - FILM LOOK DESKTOP: the data of the sensor itself (RAW), for the program on the computer only. The camera module
 * never sees this file.
 *
 * What is here, in the order it is used:
 *   1. a decoder of the lossless JPEG that Canon (CR2) and Adobe (DNG) use for the sensor data (ITU T.81, process SOF3),
 *   2. readers of the two containers: CR2 of the Canon EOS 5D Mark II, and DNG (Bayer or linear RGB, strips or tiles,
 *      lossless JPEG or uncompressed),
 *   3. the colour: black level, white level, white balance of the camera and the matrix to sRGB,
 *   4. the demosaic (the Bayer mosaic -> RGB), row by row, full size or 1/2 or 1/4,
 *   5. the first tone stage that works on 16 bit linear light (the film's tone curve, white balance, exposure ...) and
 *      hands 8 bit rows over to the film look of the engine (see fd_develop_raw() in fd_core.h).
 * Integer arithmetic in the pixel loops. The colour matrix is prepared once per photo in double precision with the plain
 * operations + - * / only (no library functions), which gives the same numbers on every system.
 *
 * Own code of the film look project. */
#ifndef FD_RAW_H
#define FD_RAW_H

/* ===================================================================== 1. lossless JPEG */
#define FR_FAST 10

typedef struct {
    uint16_t fast[1 << FR_FAST];     /* (length << 8) | symbol for codes of up to FR_FAST bits, 0 = longer */
    int32_t  maxc[18];               /* the biggest code of each length, -1 = none */
    int32_t  valp[18];               /* symbol index = code + valp[length] */
    uint8_t  sym[256];
    int      present;
} fr_huff;

typedef struct {
    int bits, nc, w, h, ss, pt;      /* precision, components, samples per line (of one component), lines, predictor, point transform */
    int dc[4];                       /* the Huffman table of each component */
    fr_huff hf[4];
    const uint8_t *scan;             /* the entropy coded data */
    size_t scan_n;
} fr_lj;

typedef struct { const uint8_t *p, *e; uint32_t acc; int n, marker; } fr_br;

static void fr_fill(fr_br *b)
{
    while (b->n <= 24) {
        uint32_t c = 0;
        if (!b->marker && b->p < b->e) {
            c = *b->p++;
            if (c == 0xFF) {
                uint32_t d = b->p < b->e ? *b->p : 0xD9;
                if (d == 0) b->p++;                     /* a stuffed 0xFF */
                else { b->marker = 1; b->p--; c = 0; }  /* a marker: the data is over, feed zeros */
            }
        }
        b->acc |= c << (24 - b->n);
        b->n += 8;
    }
}

static int fr_huff_build(fr_huff *h, const uint8_t *counts, const uint8_t *syms, int nsym)
{
    int len, k = 0, code = 0, i;
    memset(h, 0, sizeof *h);
    for (len = 1; len <= 16; len++) {
        int cnt = counts[len - 1];
        h->valp[len] = k - code;
        h->maxc[len] = -1;
        for (i = 0; i < cnt; i++) {
            if (k >= nsym || k >= 256) return -1;
            h->sym[k] = syms[k];
            if (len <= FR_FAST) {
                int fill = 1 << (FR_FAST - len), base = code << (FR_FAST - len), q;
                if (base + fill > (1 << FR_FAST)) return -1;
                for (q = 0; q < fill; q++) h->fast[base + q] = (uint16_t)((len << 8) | syms[k]);
            }
            code++; k++;
        }
        if (cnt) h->maxc[len] = code - 1;
        code <<= 1;
    }
    h->present = 1;
    return 0;
}

/* the symbol, or -1 */
static int fr_huff_get(fr_br *b, const fr_huff *h)
{
    uint32_t e;
    int l;
    if (b->n < 16) fr_fill(b);
    e = h->fast[b->acc >> (32 - FR_FAST)];
    if (e) { b->acc <<= (e >> 8); b->n -= (int)(e >> 8); return (int)(e & 255); }
    for (l = FR_FAST + 1; l <= 16; l++) {
        int32_t code = (int32_t)(b->acc >> (32 - l));
        if (code <= h->maxc[l]) {
            b->acc <<= l; b->n -= l;
            return h->sym[code + h->valp[l]];
        }
    }
    return -1;
}

/* reads the markers up to the start of the scan */
static int fr_lj_parse(const uint8_t *p, size_t n, fr_lj *j)
{
    size_t i = 2;
    int k;
    memset(j, 0, sizeof *j);
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return SE_E_BADFILE;
    while (i + 4 <= n) {
        int m, len, sl;
        const uint8_t *seg;
        if (p[i] != 0xFF) return SE_E_CORRUPT;
        m = p[i + 1];
        if (m == 0xFF) { i++; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        len = (p[i + 2] << 8) | p[i + 3];
        if (len < 2 || i + 2 + (size_t)len > n) return SE_E_TRUNCATED;
        seg = p + i + 4; sl = len - 2;
        if (m == 0xC3) {                                        /* SOF3: lossless, Huffman */
            if (sl < 6) return SE_E_CORRUPT;
            j->bits = seg[0]; j->h = (seg[1] << 8) | seg[2]; j->w = (seg[3] << 8) | seg[4]; j->nc = seg[5];
            if (j->nc < 1 || j->nc > 4 || sl < 6 + 3 * j->nc) return SE_E_CORRUPT;
            for (k = 0; k < j->nc; k++) if (seg[6 + 3 * k + 1] != 0x11) return SE_E_UNSUPPORTED;     /* no subsampling */
            if (j->bits < 8 || j->bits > 16 || j->w < 1 || j->h < 1) return SE_E_UNSUPPORTED;
        } else if (m == 0xC4) {                                 /* DHT */
            int o = 0;
            while (o + 17 <= sl) {
                int tc = seg[o] >> 4, th = seg[o] & 15, total = 0;
                for (k = 0; k < 16; k++) total += seg[o + 1 + k];
                if (total > 256 || o + 17 + total > sl || th > 3) return SE_E_CORRUPT;
                if (tc == 0 && fr_huff_build(&j->hf[th], seg + o + 1, seg + o + 17, total) != 0) return SE_E_CORRUPT;
                o += 17 + total;
            }
        } else if (m == 0xDD) {                                 /* DRI */
            if (sl >= 2 && ((seg[0] << 8) | seg[1]) != 0) return SE_E_UNSUPPORTED;
        } else if (m == 0xDA) {                                 /* SOS */
            int ns;
            if (!j->nc || sl < 1) return SE_E_CORRUPT;
            ns = seg[0];
            if (ns != j->nc || sl < 1 + 2 * ns + 3) return SE_E_UNSUPPORTED;
            for (k = 0; k < ns; k++) {
                j->dc[k] = seg[2 + 2 * k] >> 4;
                if (j->dc[k] > 3 || !j->hf[j->dc[k]].present) return SE_E_CORRUPT;
            }
            j->ss = seg[1 + 2 * ns]; j->pt = seg[3 + 2 * ns] & 15;
            if (j->ss < 1 || j->ss > 7 || j->pt != 0) return SE_E_UNSUPPORTED;      /* (a point transform is not used by cameras) */
            j->scan = p + i + 2 + len; j->scan_n = n - (i + 2 + (size_t)len);
            return SE_OK;
        } else if (m >= 0xC0 && m <= 0xCF && m != 0xC8 && m != 0xCC) {
            return SE_E_UNSUPPORTED;                            /* another kind of JPEG */
        }
        i += 2 + (size_t)len;
    }
    return SE_E_CORRUPT;
}

/* one decoded line (w * nc values, the components interleaved) is given to the sink */
typedef int (*fr_row_fn)(void *ctx, int jrow, const uint16_t *v, int n);

/* rows: room for 2 * w * nc values */
static int fr_lj_decode(const fr_lj *j, uint16_t *rows, fr_row_fn emit, void *ctx)
{
    fr_br b;
    const int nc = j->nc, n = j->w * j->nc;
    uint16_t *prev = rows, *cur = rows + n;
    int y, x, c;
    const int start = 1 << (j->bits - 1);

    b.p = j->scan; b.e = j->scan + j->scan_n; b.acc = 0; b.n = 0; b.marker = 0;
    for (y = 0; y < j->h; y++) {
        for (x = 0; x < j->w; x++) {
            for (c = 0; c < nc; c++) {
                const int idx = x * nc + c;
                int t, diff, pred;
                t = fr_huff_get(&b, &j->hf[j->dc[c]]);
                if (t < 0) return SE_E_CORRUPT;
                if (t == 0) diff = 0;
                else if (t == 16) diff = 32768;
                else {
                    int v;
                    if (b.n < t) fr_fill(&b);
                    v = (int)(b.acc >> (32 - t)); b.acc <<= t; b.n -= t;
                    diff = (v < (1 << (t - 1))) ? v - ((1 << t) - 1) : v;
                }
                if (y == 0) pred = x == 0 ? start : cur[idx - nc];
                else if (x == 0) pred = prev[idx];
                else {
                    const int ra = cur[idx - nc], rb = prev[idx], rc = prev[idx - nc];
                    switch (j->ss) {
                    case 1:  pred = ra; break;
                    case 2:  pred = rb; break;
                    case 3:  pred = rc; break;
                    case 4:  pred = ra + rb - rc; break;
                    case 5:  pred = ra + ((rb - rc) >> 1); break;
                    case 6:  pred = rb + ((ra - rc) >> 1); break;
                    default: pred = (ra + rb) >> 1; break;
                    }
                }
                cur[idx] = (uint16_t)((pred + diff) & 0xFFFF);
            }
        }
        if (emit) {
            int er = emit(ctx, y, cur, n);
            if (er) return er;
        }
        { uint16_t *tmp = prev; prev = cur; cur = tmp; }
    }
    return SE_OK;
}

/* ===================================================================== 2. containers */
typedef struct { const uint8_t *p; size_t n; int be; } fr_tf;

static uint32_t fr_u16(const fr_tf *t, size_t o)
{
    if (o + 2 > t->n) return 0;
    return t->be ? (uint32_t)((t->p[o] << 8) | t->p[o + 1]) : (uint32_t)((t->p[o + 1] << 8) | t->p[o]);
}
static uint32_t fr_u32(const fr_tf *t, size_t o)
{
    if (o + 4 > t->n) return 0;
    return t->be ? ((uint32_t)t->p[o] << 24) | ((uint32_t)t->p[o + 1] << 16) | ((uint32_t)t->p[o + 2] << 8) | t->p[o + 3]
                 : ((uint32_t)t->p[o + 3] << 24) | ((uint32_t)t->p[o + 2] << 16) | ((uint32_t)t->p[o + 1] << 8) | t->p[o];
}
static int fr_tsize(uint32_t type)
{
    static const uint8_t sz[17] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4, 0, 0, 8 };
    return type <= 16 ? sz[type] : 0;
}

typedef struct { int ok; uint32_t type, cnt; size_t off; } fr_ent;

/* the entry with this tag in the IFD at ifd (ok = 0 when there is none) */
static fr_ent fr_find(const fr_tf *t, size_t ifd, uint32_t tag)
{
    fr_ent r;
    uint32_t n, i;
    memset(&r, 0, sizeof r);
    if (ifd == 0 || ifd + 2 > t->n) return r;
    n = fr_u16(t, ifd);
    if (n > 2000 || ifd + 2 + 12 * (size_t)n > t->n) return r;
    for (i = 0; i < n; i++) {
        size_t e = ifd + 2 + 12 * (size_t)i;
        if (fr_u16(t, e) == tag) {
            size_t bytes;
            r.type = fr_u16(t, e + 2); r.cnt = fr_u32(t, e + 4);
            if (!fr_tsize(r.type)) return r;
            bytes = (size_t)fr_tsize(r.type) * r.cnt;
            r.off = bytes <= 4 ? e + 8 : fr_u32(t, e + 8);
            if (r.off + bytes > t->n || r.off + bytes < r.off) return r;
            r.ok = 1;
            return r;
        }
    }
    return r;
}
static size_t fr_next_ifd(const fr_tf *t, size_t ifd)
{
    uint32_t n;
    if (ifd == 0 || ifd + 2 > t->n) return 0;
    n = fr_u16(t, ifd);
    if (n > 2000 || ifd + 2 + 12 * (size_t)n + 4 > t->n) return 0;
    return fr_u32(t, ifd + 2 + 12 * (size_t)n);
}
/* k-th value of a BYTE / SHORT / LONG entry (0 when missing) */
static uint32_t fr_val(const fr_tf *t, const fr_ent *e, uint32_t k)
{
    if (!e->ok || k >= e->cnt) return 0;
    switch (e->type) {
    case 1: case 6: case 7: return t->p[e->off + k];
    case 3: case 8:         return fr_u16(t, e->off + 2 * (size_t)k);
    case 4: case 9: case 13: return fr_u32(t, e->off + 4 * (size_t)k);
    case 16:                return fr_u32(t, e->off + 8 * (size_t)k);
    default:                return 0;
    }
}
/* k-th value of a RATIONAL / SRATIONAL / FLOAT / DOUBLE / integer entry as a number */
static double fr_num(const fr_tf *t, const fr_ent *e, uint32_t k)
{
    if (!e->ok || k >= e->cnt) return 0.0;
    if (e->type == 5) { double d = (double)fr_u32(t, e->off + 8 * (size_t)k + 4); return d != 0.0 ? (double)fr_u32(t, e->off + 8 * (size_t)k) / d : 0.0; }
    if (e->type == 10) {
        double d = (double)(int32_t)fr_u32(t, e->off + 8 * (size_t)k + 4);
        return d != 0.0 ? (double)(int32_t)fr_u32(t, e->off + 8 * (size_t)k) / d : 0.0;
    }
    if (e->type == 9) return (double)(int32_t)fr_u32(t, e->off + 4 * (size_t)k);
    if (e->type == 8) return (double)(int16_t)fr_u16(t, e->off + 2 * (size_t)k);
    if (e->type == 11) { uint32_t u = fr_u32(t, e->off + 4 * (size_t)k); float f; memcpy(&f, &u, 4); return (double)f; }
    return (double)fr_val(t, e, k);
}

static int fr_has(const char *s, const char *needle)
{
    int i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; j++) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

/* everything the program needs to know about the sensor data of one file */
typedef struct {
    int kind;                       /* 1 = CR2, 2 = DNG */
    int rw, rh;                     /* the raw array (with the masked pixels around the picture) */
    int x0, y0, w, h;               /* the picture inside it */
    int cfa[4];                     /* colour (0 R, 1 G, 2 B) at ((y - y0) & 1) * 2 + ((x - x0) & 1); only when ncomp == 1 */
    int black[4];                   /* black level in raw units at the same positions; -1 = measure it from the masked pixels */
    int white;                      /* raw units */
    int ncomp;                      /* 1 = a Bayer mosaic, 3 = RGB samples (a linear DNG) */
    int wb[3];                      /* the camera's own white balance: multipliers R G B in 16.16, G = 65536 */
    int cam[9];                     /* camera RGB (after the white balance) -> linear sRGB, 16.16, every row sums to 65536 */
    int orient;
    int base_exp;                   /* baseline exposure in 1/100 stop (DNG), 0 otherwise */
    /* where the sensor data is */
    size_t off, len;                /* CR2: the one strip; DNG: not used */
    int nsl, sw1, sw2;              /* CR2: the slices (columns that were cut apart before the compression) */
    int compression;                /* DNG: 1 none, 7 lossless JPEG */
    int bps;                        /* bits of a sample (DNG, uncompressed) */
    int tw, th, ntiles;             /* DNG: size and number of tiles (strips are tiles that are as wide as the picture) */
    size_t t_off, t_cnt;            /* DNG: where the offsets and the byte counts of the tiles are in the file */
    int t_off_type, t_cnt_type;
    int tbe;                        /* DNG: the file is big endian */
    char make[24], model[32];
} fr_info;

/* matrices: XYZ -> camera, the way the files give them (a row for each camera colour). Camera -> sRGB the way dcraw does it:
 * rows normalised to sum 1 (so the white balanced neutral stays neutral), then inverted. */
static int fr_inv3(const double m[9], double o[9])
{
    double c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
    double det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    if (det > -1e-12 && det < 1e-12) return -1;
    o[0] = c0 / det;                          o[1] = (m[2] * m[7] - m[1] * m[8]) / det; o[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    o[3] = c1 / det;                          o[4] = (m[0] * m[8] - m[2] * m[6]) / det; o[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    o[6] = c2 / det;                          o[7] = (m[1] * m[6] - m[0] * m[7]) / det; o[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return 0;
}
static int fr_q16(double v) { return (int)(v >= 0.0 ? v * 65536.0 + 0.5 : v * 65536.0 - 0.5); }

static int fr_cam_matrix(const double xyz2cam[9], int out[9])
{
    static const double s2x[9] = { 0.412453, 0.357580, 0.180423, 0.212671, 0.715160, 0.072169, 0.019334, 0.119193, 0.950227 };
    double cr[9], inv[9], num;
    int i, j, k;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            double a = 0.0;
            for (k = 0; k < 3; k++) a += xyz2cam[i * 3 + k] * s2x[k * 3 + j];
            cr[i * 3 + j] = a;
        }
    for (i = 0; i < 3; i++) {
        num = cr[i * 3] + cr[i * 3 + 1] + cr[i * 3 + 2];
        if (num < 0.00001) return -1;
        for (j = 0; j < 3; j++) cr[i * 3 + j] /= num;
    }
    if (fr_inv3(cr, inv)) return -1;
    for (i = 0; i < 9; i++) out[i] = fr_q16(inv[i]);
    return 0;
}

/* ---- CR2 of the Canon EOS 5D Mark II (the only Canon this program decodes; others use their embedded JPEG) */
#define FR_NONE 1            /* not an error: this file has no sensor data that the program can use */

static void fr_copy_ascii(char *dst, int cap, const fr_tf *t, const fr_ent *e)
{
    uint32_t i = 0;
    dst[0] = 0;
    if (!e->ok) return;
    while ((int)i + 1 < cap && i < e->cnt && t->p[e->off + i]) { dst[i] = (char)t->p[e->off + i]; i++; }
    dst[i] = 0;
}

static int fr_parse_cr2(const uint8_t *s, size_t n, fr_info *ri)
{
    fr_tf t;
    size_t ifd0, rawifd, exif, mn;
    fr_ent e, ci, cd, sl;
    static const double c5d2[9] = { 0.4716, 0.0603, -0.0830, -0.7798, 1.5474, 0.2480, -0.1496, 0.1937, 0.6651 };
    int left, top, right, bottom, g;

    memset(ri, 0, sizeof *ri);
    if (n < 64 || s[0] != 'I' || s[1] != 'I' || s[2] != 42 || s[3] != 0 || s[8] != 'C' || s[9] != 'R') return SE_E_BADFILE;
    t.p = s; t.n = n; t.be = 0;
    ifd0 = fr_u32(&t, 4); rawifd = fr_u32(&t, 12);
    e = fr_find(&t, ifd0, 0x010f); fr_copy_ascii(ri->make, sizeof ri->make, &t, &e);
    e = fr_find(&t, ifd0, 0x0110); fr_copy_ascii(ri->model, sizeof ri->model, &t, &e);
    if (!fr_has(ri->model, "5D Mark II")) return FR_NONE;
    e = fr_find(&t, ifd0, 0x0112); ri->orient = (int)fr_val(&t, &e, 0);

    e = fr_find(&t, rawifd, 0x0103);                              /* IFD3: the sensor data, "old JPEG" = lossless */
    if (!e.ok || fr_val(&t, &e, 0) != 6) return FR_NONE;
    e = fr_find(&t, rawifd, 0x0111); ri->off = fr_val(&t, &e, 0);
    e = fr_find(&t, rawifd, 0x0117); ri->len = fr_val(&t, &e, 0);
    if (!ri->off || !ri->len || ri->off >= n) return SE_E_TRUNCATED;
    if (ri->len > n - ri->off) ri->len = n - ri->off;
    sl = fr_find(&t, rawifd, 0xc640);                              /* the slices: count, width of the first ones, width of the last */
    if (sl.ok && sl.cnt >= 3) { ri->nsl = (int)fr_val(&t, &sl, 0); ri->sw1 = (int)fr_val(&t, &sl, 1); ri->sw2 = (int)fr_val(&t, &sl, 2); }

    e = fr_find(&t, ifd0, 0x8769); exif = fr_val(&t, &e, 0);
    e = fr_find(&t, exif, 0x927c); mn = e.ok ? e.off : 0;
    ci = fr_find(&t, mn, 0x00e0);                                  /* SensorInfo: the sensor and its picture area */
    cd = fr_find(&t, mn, 0x4001);                                  /* ColorData: the white balance of the shot */
    if (!ci.ok || ci.cnt < 9 || !cd.ok || cd.cnt < 70 || fr_val(&t, &cd, 0) != 6) return FR_NONE;
    ri->rw = (int)fr_val(&t, &ci, 1); ri->rh = (int)fr_val(&t, &ci, 2);
    left = (int)fr_val(&t, &ci, 5); top = (int)fr_val(&t, &ci, 6); right = (int)fr_val(&t, &ci, 7); bottom = (int)fr_val(&t, &ci, 8);
    if (ri->rw < 16 || ri->rh < 16 || ri->rw > 20000 || ri->rh > 20000 || left < 0 || top < 0 || right < left || bottom < top
        || right >= ri->rw || bottom >= ri->rh) return SE_E_CORRUPT;
    ri->x0 = left; ri->y0 = top; ri->w = right - left + 1; ri->h = bottom - top + 1;
    ri->ncomp = 1;
    /* the mosaic of the 5D Mark II starts with G B / R G at the corner of the picture (the corner is at even coordinates) */
    if ((left & 1) || (top & 1)) return SE_E_UNSUPPORTED;
    ri->cfa[0] = 1; ri->cfa[1] = 2; ri->cfa[2] = 0; ri->cfa[3] = 1;
    ri->black[0] = ri->black[1] = ri->black[2] = ri->black[3] = -1;
    ri->white = 15600;
    {
        int wr = (int)fr_val(&t, &cd, 63), wg1 = (int)fr_val(&t, &cd, 64), wg2 = (int)fr_val(&t, &cd, 65), wb = (int)fr_val(&t, &cd, 66);
        g = wg1 + wg2;
        if (wr <= 0 || wb <= 0 || g <= 0) return SE_E_CORRUPT;
        ri->wb[0] = (int)(((int64_t)wr * 2 * 65536) / g); ri->wb[1] = 65536; ri->wb[2] = (int)(((int64_t)wb * 2 * 65536) / g);
    }
    ri->kind = 1;
    if (fr_cam_matrix(c5d2, ri->cam)) return SE_E_CORRUPT;
    return SE_OK;
}

/* ---- DNG (Adobe): Bayer mosaic or linear RGB, strips or tiles, lossless JPEG or uncompressed */
static double fr_ratio(const fr_tf *t, const fr_ent *e, uint32_t k) { return fr_num(t, e, k); }

/* the entry of a tag in the raw IFD, else in IFD0 */
static fr_ent fr_find2(const fr_tf *t, size_t a, size_t b, uint32_t tag)
{
    fr_ent e = fr_find(t, a, tag);
    return e.ok ? e : fr_find(t, b, tag);
}

static int fr_parse_dng(const uint8_t *s, size_t n, fr_info *ri)
{
    fr_tf t;
    size_t ifd0, queue[48], qn = 0, qi, raw = 0, next;
    int64_t bestarea = 0;
    fr_ent e, cm, c1, c2;
    int w, h, spp, photo, comp, bps, k, i, aa[4], cox = 0, coy = 0, cw, ch, rr = 1, rc = 1, nb;
    double xyz2cam[9], neu[3];
    uint32_t ill1, ill2;

    memset(ri, 0, sizeof *ri);
    if (n < 32) return SE_E_BADFILE;
    if (s[0] == 'I' && s[1] == 'I' && s[2] == 42 && s[3] == 0) t.be = 0;
    else if (s[0] == 'M' && s[1] == 'M' && s[2] == 0 && s[3] == 42) t.be = 1;
    else return SE_E_BADFILE;
    t.p = s; t.n = n;
    ifd0 = fr_u32(&t, 4);
    e = fr_find(&t, ifd0, 0xC612);                                 /* DNGVersion: without it this is some other TIFF */
    if (!e.ok) return FR_NONE;
    e = fr_find(&t, ifd0, 0x010f); fr_copy_ascii(ri->make, sizeof ri->make, &t, &e);
    e = fr_find(&t, ifd0, 0x0110); fr_copy_ascii(ri->model, sizeof ri->model, &t, &e);
    e = fr_find(&t, ifd0, 0x0112); ri->orient = (int)fr_val(&t, &e, 0);

    /* the IFDs: IFD0, the ones it chains to, and all the SubIFDs of them */
    queue[qn++] = ifd0;
    for (next = fr_next_ifd(&t, ifd0), k = 0; next && k < 6 && qn < 48; k++, next = fr_next_ifd(&t, next)) queue[qn++] = next;
    for (qi = 0; qi < qn; qi++) {
        e = fr_find(&t, queue[qi], 0x014A);
        for (k = 0; e.ok && k < (int)e.cnt && k < 16 && qn < 48; k++) queue[qn++] = fr_u32(&t, e.off + 4 * (size_t)k);
    }
    for (qi = 0; qi < qn; qi++) {                                  /* the biggest full size image of the sensor */
        size_t f = queue[qi];
        fr_ent nf = fr_find(&t, f, 0x00FE), ph = fr_find(&t, f, 0x0106), ew = fr_find(&t, f, 0x0100), eh = fr_find(&t, f, 0x0101);
        int64_t area;
        if (!ew.ok || !eh.ok || !ph.ok) continue;
        if (nf.ok && (fr_val(&t, &nf, 0) & 1)) continue;           /* a reduced size image: a preview */
        { uint32_t p = fr_val(&t, &ph, 0); if (p != 32803 && p != 34892) continue; }
        area = (int64_t)fr_val(&t, &ew, 0) * (int64_t)fr_val(&t, &eh, 0);
        if (area > bestarea) { bestarea = area; raw = f; }
    }
    if (!raw) return FR_NONE;

    e = fr_find(&t, raw, 0x0100); w = (int)fr_val(&t, &e, 0);
    e = fr_find(&t, raw, 0x0101); h = (int)fr_val(&t, &e, 0);
    e = fr_find(&t, raw, 0x0106); photo = (int)fr_val(&t, &e, 0);
    e = fr_find(&t, raw, 0x0103); comp = (int)fr_val(&t, &e, 0);
    e = fr_find(&t, raw, 0x0115); spp = e.ok ? (int)fr_val(&t, &e, 0) : 1;
    e = fr_find(&t, raw, 0x0102); bps = e.ok ? (int)fr_val(&t, &e, 0) : 16;
    if (w < 16 || h < 16 || w > 30000 || h > 30000 || (int64_t)w * h > 150000000) return FR_NONE;
    if (comp != 1 && comp != 7) return FR_NONE;                    /* (lossy and deflate compressed DNG: the JPEG route) */
    if (photo == 32803 && spp != 1) return FR_NONE;
    if (photo == 34892 && spp != 3) return FR_NONE;
    e = fr_find(&t, raw, 0x011C); if (e.ok && fr_val(&t, &e, 0) != 1) return FR_NONE;        /* planar configuration: chunky only */
    e = fr_find(&t, raw, 0x0153); if (e.ok && fr_val(&t, &e, 0) != 1) return FR_NONE;        /* sample format: integers only */
    e = fr_find(&t, raw, 0xC618); if (e.ok) return FR_NONE;                                  /* a linearization table */
    if (comp == 1 && bps != 16 && bps != 8) return FR_NONE;
    ri->kind = 2; ri->rw = w; ri->rh = h; ri->ncomp = spp; ri->compression = comp; ri->bps = bps; ri->tbe = t.be;

    /* where the data is: tiles, or strips (a strip is a tile as wide as the picture) */
    {
        fr_ent to = fr_find(&t, raw, 0x0144), tc = fr_find(&t, raw, 0x0145), tw = fr_find(&t, raw, 0x0142), tl = fr_find(&t, raw, 0x0143);
        if (to.ok && tc.ok && tw.ok && tl.ok) {
            ri->tw = (int)fr_val(&t, &tw, 0); ri->th = (int)fr_val(&t, &tl, 0);
        } else {
            fr_ent rps = fr_find(&t, raw, 0x0116);
            to = fr_find(&t, raw, 0x0111); tc = fr_find(&t, raw, 0x0117);
            ri->tw = w; ri->th = rps.ok ? (int)fr_val(&t, &rps, 0) : h;
            if (ri->th < 1 || ri->th > h) ri->th = h;
        }
        if (!to.ok || !tc.ok || ri->tw < 1 || ri->th < 1 || ri->tw > 65535 || ri->th > 65535) return SE_E_CORRUPT;
        ri->ntiles = ((w + ri->tw - 1) / ri->tw) * ((h + ri->th - 1) / ri->th);
        if ((int)to.cnt < ri->ntiles || (int)tc.cnt < ri->ntiles) return SE_E_CORRUPT;
        ri->t_off = to.off; ri->t_off_type = (int)to.type; ri->t_cnt = tc.off; ri->t_cnt_type = (int)tc.type;
    }

    /* the picture inside the array: the active area, and in it the crop */
    aa[0] = 0; aa[1] = 0; aa[2] = h; aa[3] = w;
    e = fr_find2(&t, raw, ifd0, 0xC68D);
    if (e.ok && e.cnt >= 4) for (k = 0; k < 4; k++) aa[k] = (int)fr_val(&t, &e, (uint32_t)k);
    if (aa[0] < 0 || aa[1] < 0 || aa[2] > h || aa[3] > w || aa[2] - aa[0] < 16 || aa[3] - aa[1] < 16) return SE_E_CORRUPT;
    cw = aa[3] - aa[1]; ch = aa[2] - aa[0];
    e = fr_find2(&t, raw, ifd0, 0xC61F);
    if (e.ok && e.cnt >= 2) { cox = (int)(fr_ratio(&t, &e, 0) + 0.5); coy = (int)(fr_ratio(&t, &e, 1) + 0.5); }    /* the origin: horizontal, vertical */
    e = fr_find2(&t, raw, ifd0, 0xC620);
    if (e.ok && e.cnt >= 2) { cw = (int)(fr_ratio(&t, &e, 0) + 0.5); ch = (int)(fr_ratio(&t, &e, 1) + 0.5); }
    if (cox < 0 || coy < 0 || cw < 16 || ch < 16 || aa[1] + cox + cw > w || aa[0] + coy + ch > h) return SE_E_CORRUPT;
    ri->x0 = aa[1] + cox; ri->y0 = aa[0] + coy; ri->w = cw; ri->h = ch;

    /* the mosaic: colour of the pixels at the corner of the picture */
    if (spp == 1) {
        int pat[4];
        e = fr_find2(&t, raw, ifd0, 0x828D);
        if (!e.ok || fr_val(&t, &e, 0) != 2 || fr_val(&t, &e, 1) != 2) return FR_NONE;                  /* only 2 x 2 patterns (Bayer) */
        e = fr_find2(&t, raw, ifd0, 0x828E);
        if (!e.ok || e.cnt != 4) return FR_NONE;
        for (k = 0; k < 4; k++) { pat[k] = (int)fr_val(&t, &e, (uint32_t)k); if (pat[k] > 2) return FR_NONE; }
        for (k = 0; k < 4; k++) ri->cfa[k] = pat[(((ri->y0 + (k >> 1)) & 1) << 1) | ((ri->x0 + (k & 1)) & 1)];
        { int cnt[3] = { 0, 0, 0 }; for (k = 0; k < 4; k++) cnt[ri->cfa[k]]++; if (cnt[0] != 1 || cnt[2] != 1 || cnt[1] != 2) return FR_NONE; }
    } else { ri->cfa[0] = 0; ri->cfa[1] = 1; ri->cfa[2] = 2; ri->cfa[3] = 1; }

    /* black and white */
    e = fr_find2(&t, raw, ifd0, 0xC619);
    if (e.ok && e.cnt >= 2) { rr = (int)fr_val(&t, &e, 0); rc = (int)fr_val(&t, &e, 1); }
    if (rr < 1 || rc < 1 || rr > 8 || rc > 8) { rr = 1; rc = 1; }
    e = fr_find2(&t, raw, ifd0, 0xC61A);
    nb = e.ok ? (int)e.cnt : 0;
    for (k = 0; k < 4; k++) ri->black[k] = 0;
    if (spp == 1) {
        for (k = 0; k < 4; k++) {
            int idx = (((ri->y0 + (k >> 1)) % rr) * rc + ((ri->x0 + (k & 1)) % rc));
            if (idx < nb) ri->black[k] = (int)(fr_ratio(&t, &e, (uint32_t)idx) + 0.5);
        }
    } else {
        for (k = 0; k < 3; k++) { int idx = nb >= 3 ? k : 0; if (idx < nb) ri->black[k] = (int)(fr_ratio(&t, &e, (uint32_t)idx) + 0.5); }
        ri->black[3] = ri->black[1];
    }
    e = fr_find2(&t, raw, ifd0, 0xC61D);
    ri->white = e.ok ? (int)(fr_ratio(&t, &e, 0) + 0.5) : (comp == 7 ? 65535 : (1 << bps) - 1);
    if (ri->white < 255 || ri->white > 65535) return SE_E_CORRUPT;
    for (k = 0; k < 4; k++) if (ri->black[k] < 0 || ri->black[k] >= ri->white - 64) return SE_E_CORRUPT;

    /* colour: the matrix from XYZ to the camera (the one for daylight when there are two), and the white balance of the shot */
    c1 = fr_find(&t, ifd0, 0xC621); c2 = fr_find(&t, ifd0, 0xC622);
    if (!c1.ok) c1 = fr_find(&t, raw, 0xC621);
    if (!c2.ok) c2 = fr_find(&t, raw, 0xC622);
    e = fr_find(&t, ifd0, 0xC65A); ill1 = e.ok ? fr_val(&t, &e, 0) : 0;
    e = fr_find(&t, ifd0, 0xC65B); ill2 = e.ok ? fr_val(&t, &e, 0) : 0;
    if (c2.ok && c2.cnt >= 9 && (ill2 == 21 || !(c1.ok && c1.cnt >= 9 && ill1 == 21))) cm = c2;
    else if (c1.ok && c1.cnt >= 9) cm = c1;
    else return FR_NONE;
    if (cm.cnt != 9) return FR_NONE;                                                           /* a three colour camera */
    for (k = 0; k < 9; k++) xyz2cam[k] = fr_ratio(&t, &cm, (uint32_t)k);
    if (fr_cam_matrix(xyz2cam, ri->cam)) return SE_E_CORRUPT;
    e = fr_find(&t, ifd0, 0xC628);                                                              /* AsShotNeutral: the camera's response to the white of the shot */
    if (e.ok && e.cnt >= 3) { for (k = 0; k < 3; k++) neu[k] = fr_ratio(&t, &e, (uint32_t)k); }
    else {
        static const double d65[3] = { 0.950456, 1.0, 1.089058 };
        double xy[2], X, Z;
        e = fr_find(&t, ifd0, 0xC629);                                                          /* AsShotWhiteXY, or the daylight white */
        X = d65[0]; Z = d65[2];
        if (e.ok && e.cnt >= 2) {
            xy[0] = fr_ratio(&t, &e, 0); xy[1] = fr_ratio(&t, &e, 1);
            if (xy[1] > 0.01) { X = xy[0] / xy[1]; Z = (1.0 - xy[0] - xy[1]) / xy[1]; }
        }
        for (i = 0; i < 3; i++) neu[i] = xyz2cam[i * 3] * X + xyz2cam[i * 3 + 1] * 1.0 + xyz2cam[i * 3 + 2] * Z;
    }
    if (neu[0] <= 0.0001 || neu[1] <= 0.0001 || neu[2] <= 0.0001) return SE_E_CORRUPT;
    ri->wb[0] = fr_q16(neu[1] / neu[0]); ri->wb[1] = 65536; ri->wb[2] = fr_q16(neu[1] / neu[2]);
    e = fr_find(&t, ifd0, 0xC62A);
    if (e.ok) ri->base_exp = (int)(fr_ratio(&t, &e, 0) * 100.0 + (fr_ratio(&t, &e, 0) >= 0 ? 0.5 : -0.5));
    return SE_OK;
}

/* ===================================================================== sinks of the decoder */
/* a CR2: the lossless JPEG holds the picture cut into vertical slices, one after the other, each slice row by row */
typedef struct { uint16_t *raw; int W, H, nsl, sw1, sw2; } fr_cr2_sink;

static int fr_cr2_emit(void *ctx, int jrow, const uint16_t *v, int n)
{
    const fr_cr2_sink *s = (const fr_cr2_sink *)ctx;
    int64_t pos = (int64_t)jrow * n, per = (int64_t)s->sw1 * s->H;       /* position of the first value, samples in a slice */
    while (n > 0) {
        int sli, sw, row, col, run;
        int64_t within;
        sli = per > 0 ? (int)(pos / per) : 0;
        if (sli > s->nsl) return SE_E_CORRUPT;
        if (sli >= s->nsl) { sw = s->sw2; sli = s->nsl; } else sw = s->sw1;
        within = pos - (int64_t)sli * per;
        if (sw <= 0) return SE_E_CORRUPT;
        row = (int)(within / sw); col = (int)(within % sw) + sli * s->sw1;
        if (row >= s->H) return SE_E_CORRUPT;
        run = sw - (int)(within % sw);
        if (run > n) run = n;
        if (col + run > s->W) return SE_E_CORRUPT;
        memcpy(s->raw + (size_t)row * s->W + col, v, (size_t)run * sizeof(uint16_t));
        v += run; n -= run; pos += run;
    }
    return SE_OK;
}

/* a rectangle of a bigger array (a DNG tile or strip); values past the edge are dropped */
typedef struct { uint16_t *dst; size_t stride; int x0, y0, cols, rows; } fr_rect_sink;

static int fr_rect_emit(void *ctx, int jrow, const uint16_t *v, int n)
{
    const fr_rect_sink *s = (const fr_rect_sink *)ctx;
    if (jrow >= s->rows) return SE_OK;
    if (n > s->cols) n = s->cols;
    if (n > 0) memcpy(s->dst + (size_t)(s->y0 + jrow) * s->stride + s->x0, v, (size_t)n * sizeof(uint16_t));
    return SE_OK;
}

/* the sensor data of a CR2 into raw (rw * rh values, allocated by the caller) */
static int fr_decode_cr2(const uint8_t *s, const fr_info *ri, uint16_t *raw, uint16_t *rowbuf_2x)
{
    fr_lj j;
    fr_cr2_sink sk;
    int rc = fr_lj_parse(s + ri->off, ri->len, &j);
    if (rc) return rc;
    if (j.nc * j.w != ri->rw || j.h != ri->rh) {                 /* the sensor does not have the size the file says */
        if (!(ri->nsl > 0 && (int64_t)j.nc * j.w * j.h == (int64_t)ri->rw * ri->rh)) return SE_E_CORRUPT;
    }
    sk.raw = raw; sk.W = ri->rw; sk.H = ri->rh; sk.nsl = ri->nsl; sk.sw1 = ri->sw1; sk.sw2 = ri->sw2;
    if (ri->nsl <= 0) { sk.nsl = 0; sk.sw1 = ri->rw; sk.sw2 = ri->rw; }      /* one slice = the whole picture */
    return fr_lj_decode(&j, rowbuf_2x, fr_cr2_emit, &sk);
}

/* the k-th SHORT or LONG of an array of tile offsets or byte counts */
static size_t fr_arr_val(const fr_tf *t, size_t arr, int type, int k)
{
    return type == 3 ? (size_t)fr_u16(t, arr + 2 * (size_t)k) : (size_t)fr_u32(t, arr + 4 * (size_t)k);
}

/* the sensor data of a DNG into raw (rw * rh * ncomp values, allocated by the caller); rowbuf: fr_dng_rowbuf(ri) values */
static size_t fr_dng_rowbuf(const fr_info *ri) { return 2 * (size_t)ri->tw * (size_t)ri->ncomp + 16; }

static int fr_decode_dng(const uint8_t *s, size_t n, const fr_info *ri, uint16_t *raw, uint16_t *rowbuf)
{
    fr_tf t;
    int across = (ri->rw + ri->tw - 1) / ri->tw, down = (ri->rh + ri->th - 1) / ri->th, tx, ty;
    const size_t spp = (size_t)ri->ncomp;
    t.p = s; t.n = n; t.be = ri->tbe;
    for (ty = 0; ty < down; ty++)
        for (tx = 0; tx < across; tx++) {
            const int k = ty * across + tx;
            const size_t off = fr_arr_val(&t, ri->t_off, ri->t_off_type, k), cnt = fr_arr_val(&t, ri->t_cnt, ri->t_cnt_type, k);
            const int cols = (ri->rw - tx * ri->tw < ri->tw) ? ri->rw - tx * ri->tw : ri->tw;
            const int rows = (ri->rh - ty * ri->th < ri->th) ? ri->rh - ty * ri->th : ri->th;
            if (off > n || cnt > n - off) return SE_E_TRUNCATED;
            if (ri->compression == 7) {
                fr_lj j;
                fr_rect_sink sk;
                int rc = fr_lj_parse(s + off, cnt, &j);
                if (rc) return rc;
                if ((int64_t)j.nc * j.w != (int64_t)ri->tw * (int64_t)spp || j.h < rows) return SE_E_CORRUPT;
                sk.dst = raw; sk.stride = (size_t)ri->rw * spp; sk.x0 = tx * ri->tw * (int)spp; sk.y0 = ty * ri->th;
                sk.cols = cols * (int)spp; sk.rows = rows;
                rc = fr_lj_decode(&j, rowbuf, fr_rect_emit, &sk);
                if (rc) return rc;
            } else {
                const size_t bytes = (size_t)(ri->bps / 8), rowv = (size_t)ri->tw * spp;
                int r;
                size_t c;
                if (cnt < (size_t)rows * rowv * bytes) return SE_E_TRUNCATED;
                for (r = 0; r < rows; r++) {
                    uint16_t *d = raw + ((size_t)(ty * ri->th + r) * ri->rw + (size_t)tx * ri->tw) * spp;
                    const size_t base = off + (size_t)r * rowv * bytes;
                    for (c = 0; c < (size_t)cols * spp; c++)
                        d[c] = bytes == 2 ? (uint16_t)fr_u16(&t, base + c * 2) : (uint16_t)s[base + c];
                }
            }
        }
    return SE_OK;
}

/* ===================================================================== 3. from the mosaic to RGB rows */
/* A raw decoded picture ready to be turned into rows. The sensor values are scaled, white balanced and clipped (the clip is the
 * same for all three colours, so a blown highlight stays white) while the rows are made. */
typedef struct {
    fr_info ri;
    const uint16_t *raw;             /* ri.rw * ri.rh values (ncomp == 1) or ri.rw * ri.rh * 3 (a linear DNG) */
    int black[4];                    /* resolved black levels */
    int64_t mul[4];                  /* 16.16 multiplier of each mosaic position: 65535 / (white - black) * white balance */
    int cam[9];
    /* a window of scaled rows (with a margin on both sides) and of interpolated green rows */
    uint16_t *win;                   /* 8 rows of (w + 8) */
    int win_row[8];
    uint16_t *gr;                    /* 3 rows of (w + 8): the interpolated green */
    int gr_row[3];
    int stride;
} fr_ctx;

#define FR_PAD 4

static int fr_pos(const fr_ctx *c, int x, int y) { (void)c; return ((y & 1) << 1) | (x & 1); }

/* measures the black level from the masked pixels beside the picture (the optical black of the sensor), per mosaic position */
static void fr_measure_black(fr_ctx *c)
{
    const fr_info *ri = &c->ri;
    int p, x, y, xa = 16, xb = ri->x0 - 16, ya = ri->y0, yb = ri->y0 + ri->h;
    int64_t sum[4] = { 0, 0, 0, 0 }, cnt[4] = { 0, 0, 0, 0 };
    if (xb - xa < 16) {                                  /* no masked columns on the left: the right side, else a guess */
        xa = ri->x0 + ri->w + 16; xb = ri->rw - 16;
    }
    if (xb - xa >= 16) {
        for (y = ya; y < yb; y += 2)
            for (x = xa; x < xb; x++) {
                p = ((y - ri->y0) & 1) * 2 + ((x - ri->x0) & 1);
                sum[p] += c->raw[(size_t)y * ri->rw + x]; cnt[p]++;
                p = ((y + 1 - ri->y0) & 1) * 2 + ((x - ri->x0) & 1);
                if (y + 1 < yb) { sum[p] += c->raw[(size_t)(y + 1) * ri->rw + x]; cnt[p]++; }
            }
    }
    for (p = 0; p < 4; p++) c->black[p] = cnt[p] > 0 ? (int)((sum[p] + cnt[p] / 2) / cnt[p]) : 1024;
}

/* the multipliers: the colour of each mosaic position gets its white balance */
static void fr_prepare(fr_ctx *c, const fr_info *ri, const uint16_t *raw)
{
    int p;
    c->ri = *ri; c->raw = raw;
    if (ri->black[0] < 0) fr_measure_black(c);
    else for (p = 0; p < 4; p++) c->black[p] = ri->black[p];
    for (p = 0; p < 4; p++) {
        int span = ri->white - c->black[p], col = ri->cfa[p];
        if (span < 64) span = 64;
        c->mul[p] = ((int64_t)65535 * 65536 / span) * ri->wb[col] >> 16;
    }
    memcpy(c->cam, ri->cam, sizeof c->cam);
}

static uint32_t fr_scale(const fr_ctx *c, int pos, int v)
{
    int64_t n;
    v -= c->black[pos];
    if (v <= 0) return 0;
    n = ((int64_t)v * c->mul[pos]) >> 16;
    return n > 65535 ? 65535u : (uint32_t)n;
}

/* the matrix: camera RGB (white balanced, 0..65535) -> linear sRGB, clipped */
static void fr_matrix(const fr_ctx *c, int r, int g, int b, uint16_t *out)
{
    int64_t o0 = ((int64_t)c->cam[0] * r + (int64_t)c->cam[1] * g + (int64_t)c->cam[2] * b + 32768) >> 16;
    int64_t o1 = ((int64_t)c->cam[3] * r + (int64_t)c->cam[4] * g + (int64_t)c->cam[5] * b + 32768) >> 16;
    int64_t o2 = ((int64_t)c->cam[6] * r + (int64_t)c->cam[7] * g + (int64_t)c->cam[8] * b + 32768) >> 16;
    out[0] = (uint16_t)(o0 < 0 ? 0 : (o0 > 65535 ? 65535 : o0));
    out[1] = (uint16_t)(o1 < 0 ? 0 : (o1 > 65535 ? 65535 : o1));
    out[2] = (uint16_t)(o2 < 0 ? 0 : (o2 > 65535 ? 65535 : o2));
}

/* a linear DNG: the samples are RGB (camera RGB) already, nothing has to be interpolated. The block average at 1/div size */
static void fr_pix_linear(const fr_ctx *c, int ox, int oy, int div, uint16_t *out)
{
    const fr_info *ri = &c->ri;
    uint32_t sum[3] = { 0, 0, 0 }, cnt = 0;
    int dy, dx, ch, rgb[3];
    for (dy = 0; dy < div; dy++) {
        int y = oy * div + dy;
        if (y >= ri->h) y = ri->h - 1;
        for (dx = 0; dx < div; dx++) {
            int x = ox * div + dx;
            const uint16_t *p;
            if (x >= ri->w) x = ri->w - 1;
            p = c->raw + ((size_t)(ri->y0 + y) * ri->rw + ri->x0 + x) * 3;
            for (ch = 0; ch < 3; ch++) sum[ch] += fr_scale(c, ch, p[ch]);
            cnt++;
        }
    }
    for (ch = 0; ch < 3; ch++) rgb[ch] = (int)((sum[ch] + cnt / 2) / cnt);
    fr_matrix(c, rgb[0], rgb[1], rgb[2], out);
}

/* one pixel of the picture at 1/div size: the average of a block of the mosaic, colour by colour (no interpolation at all).
 * out: 3 16 bit values, the linear sRGB. */
static void fr_pix_binned(const fr_ctx *c, int ox, int oy, int div, uint16_t *out)
{
    const fr_info *ri = &c->ri;
    uint32_t sum[3] = { 0, 0, 0 }, cnt[3] = { 0, 0, 0 };
    int rgb[3], dy, dx;
    if (ri->ncomp == 3) { fr_pix_linear(c, ox, oy, div, out); return; }
    for (dy = 0; dy < div; dy++) {
        int y = oy * div + dy;
        if (y >= ri->h) y = ri->h - 1;
        for (dx = 0; dx < div; dx++) {
            int x = ox * div + dx, pos, col;
            if (x >= ri->w) x = ri->w - 1;
            pos = ((y & 1) << 1) | (x & 1);
            col = ri->cfa[pos];
            sum[col] += fr_scale(c, pos, c->raw[(size_t)(ri->y0 + y) * ri->rw + ri->x0 + x]);
            cnt[col]++;
        }
    }
    rgb[0] = cnt[0] ? (int)((sum[0] + cnt[0] / 2) / cnt[0]) : 0;
    rgb[1] = cnt[1] ? (int)((sum[1] + cnt[1] / 2) / cnt[1]) : 0;
    rgb[2] = cnt[2] ? (int)((sum[2] + cnt[2] / 2) / cnt[2]) : 0;
    fr_matrix(c, rgb[0], rgb[1], rgb[2], out);
}

/* one row of the picture at 1/div size: out = 3 * ow values */
static void fr_row_binned(const fr_ctx *c, int oy, int div, int ow, uint16_t *out)
{
    int ox;
    for (ox = 0; ox < ow; ox++) fr_pix_binned(c, ox, oy, div, out + 3 * ox);
}

/* ---- full size: Hamilton-Adams. The green is interpolated along the direction in which the picture changes less, the red and
 * the blue as differences to the green (a colour difference changes slowly, so it is safe to average). */
static int fr_mirror(int v, int n) { if (v < 0) v = -v; if (v >= n) v = 2 * (n - 1) - v; return v < 0 ? 0 : (v >= n ? n - 1 : v); }

/* the scaled row r of the picture (rows outside are mirrored: the same mosaic colour) in the window */
/* memory of the full size demosaic: a window of 8 scaled rows and 3 rows of green */
static int fr_full_begin(fr_ctx *c)
{
    int i;
    c->stride = c->ri.w + 2 * FR_PAD;
    c->win = (uint16_t *)malloc((size_t)c->stride * (8 + 3) * sizeof(uint16_t));
    if (!c->win) return -1;
    c->gr = c->win + (size_t)c->stride * 8;
    for (i = 0; i < 8; i++) c->win_row[i] = -1000000;
    for (i = 0; i < 3; i++) c->gr_row[i] = -1000000;
    return 0;
}
static void fr_full_end(fr_ctx *c) { free(c->win); c->win = 0; c->gr = 0; }

static const uint16_t *fr_win_row(fr_ctx *c, int r)
{
    const fr_info *ri = &c->ri;
    int slot = r & 7, rr, x;
    uint16_t *d = c->win + (size_t)slot * c->stride;
    if (c->win_row[slot] == r) return d + FR_PAD;
    rr = fr_mirror(r, ri->h);
    {
        const uint16_t *src = c->raw + (size_t)(ri->y0 + rr) * ri->rw + ri->x0;
        for (x = 0; x < ri->w; x++) d[FR_PAD + x] = (uint16_t)fr_scale(c, ((rr & 1) << 1) | (x & 1), src[x]);
        for (x = 1; x <= FR_PAD; x++) {                        /* mirrored margins: same parity, so the same colour */
            d[FR_PAD - x] = d[FR_PAD + fr_mirror(-x, ri->w)];
            d[FR_PAD + ri->w - 1 + x] = d[FR_PAD + fr_mirror(ri->w - 1 + x, ri->w)];
        }
    }
    c->win_row[slot] = r;
    return d + FR_PAD;
}

static int fr_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int fr_absi(int v) { return v < 0 ? -v : v; }

/* the interpolated green of row r into g (w values, with margins around) */
static void fr_green_row(fr_ctx *c, int r, uint16_t *g)
{
    const fr_info *ri = &c->ri;
    const uint16_t *m2 = fr_win_row(c, r - 2), *m1 = fr_win_row(c, r - 1), *z = fr_win_row(c, r), *p1 = fr_win_row(c, r + 1), *p2 = fr_win_row(c, r + 2);
    int x;
    for (x = 0; x < ri->w; x++) {
        const int rr = fr_mirror(r, ri->h);
        if (ri->cfa[((rr & 1) << 1) | (x & 1)] == 1) { g[FR_PAD + x] = z[x]; continue; }
        {
            const int v = z[x];
            const int gl = z[x - 1], gr = z[x + 1], gu = m1[x], gd = p1[x];
            const int dh = fr_absi(gl - gr) + fr_absi(2 * v - z[x - 2] - z[x + 2]);
            const int dv = fr_absi(gu - gd) + fr_absi(2 * v - m2[x] - p2[x]);
            int est, lo, hi;
            if (dh < dv)      est = ((gl + gr) * 2 + (2 * v - z[x - 2] - z[x + 2]) + 2) >> 2;
            else if (dh > dv) est = ((gu + gd) * 2 + (2 * v - m2[x] - p2[x]) + 2) >> 2;
            else              est = ((gl + gr + gu + gd) * 2 + (4 * v - z[x - 2] - z[x + 2] - m2[x] - p2[x]) + 4) >> 3;
            lo = gl < gr ? gl : gr; if (gu < lo) lo = gu; if (gd < lo) lo = gd;
            hi = gl > gr ? gl : gr; if (gu > hi) hi = gu; if (gd > hi) hi = gd;
            g[FR_PAD + x] = (uint16_t)fr_clampi(est, lo, hi);
        }
    }
    for (x = 1; x <= FR_PAD; x++) {
        g[FR_PAD - x] = g[FR_PAD + fr_mirror(-x, ri->w)];
        g[FR_PAD + ri->w - 1 + x] = g[FR_PAD + fr_mirror(ri->w - 1 + x, ri->w)];
    }
}

static uint16_t *fr_green_get(fr_ctx *c, int r)
{
    int slot = ((r % 3) + 3) % 3;
    uint16_t *g = c->gr + (size_t)slot * c->stride + FR_PAD;
    if (c->gr_row[slot] != r) { fr_green_row(c, r, g - FR_PAD); c->gr_row[slot] = r; }
    return g;
}

/* one row at full size: out = 3 * w values of linear sRGB */
static void fr_row_full_bayer(fr_ctx *c, int y, uint16_t *out)
{
    const fr_info *ri = &c->ri;
    const uint16_t *zm = fr_win_row(c, y - 1), *z = fr_win_row(c, y), *zp = fr_win_row(c, y + 1);
    const uint16_t *gm = fr_green_get(c, y - 1), *g0 = fr_green_get(c, y), *gp = fr_green_get(c, y + 1);
    int x;
    for (x = 0; x < ri->w; x++) {
        const int col = ri->cfa[((fr_mirror(y, ri->h) & 1) << 1) | (x & 1)];
        int R, G, B;
        G = g0[x];
        if (col == 1) {
            /* the horizontal neighbours of a green pixel are red or blue, the vertical ones the other colour */
            const int hcol = ri->cfa[((fr_mirror(y, ri->h) & 1) << 1) | ((x + 1) & 1)];
            const int dh = ((z[x - 1] - g0[x - 1]) + (z[x + 1] - g0[x + 1]) + 1) >> 1;
            const int dv = ((zm[x] - gm[x]) + (zp[x] - gp[x]) + 1) >> 1;
            if (hcol == 0) { R = G + dh; B = G + dv; } else { B = G + dh; R = G + dv; }
        } else {
            /* the other colour sits on the diagonals: along the diagonal that changes less */
            const int a = zm[x - 1], b = zm[x + 1], d = zp[x - 1], e = zp[x + 1];
            const int da = zm[x - 1] - gm[x - 1], db = zm[x + 1] - gm[x + 1], dd = zp[x - 1] - gp[x - 1], de = zp[x + 1] - gp[x + 1];
            const int g1 = fr_absi(a - e), g2 = fr_absi(b - d);
            int diff;
            if (g1 < g2)      diff = (da + de + 1) >> 1;
            else if (g1 > g2) diff = (db + dd + 1) >> 1;
            else              diff = (da + db + dd + de + 2) >> 2;
            if (col == 0) { R = z[x]; B = G + diff; } else { B = z[x]; R = G + diff; }
        }
        fr_matrix(c, fr_clampi(R, 0, 65535), fr_clampi(G, 0, 65535), fr_clampi(B, 0, 65535), out + 3 * x);
    }
}

static void fr_row_full(fr_ctx *c, int y, uint16_t *out)
{
    if (c->ri.ncomp == 3) { int ox; for (ox = 0; ox < c->ri.w; ox++) fr_pix_linear(c, ox, y, 1, out + 3 * ox); }
    else fr_row_full_bayer(c, y, out);
}

/* ===================================================================== 4. the first tone stage, on 16 bit linear light */
/* The film look of the engine works on 8 bit rows, and its first step is a table for each colour (curves, contrast, ...). For
 * RAW that step is made here instead, from the 16 bit linear light, so that nothing is thrown away before the tone is
 * decided: white balance and exposure are done on the linear light, the result is kept with 8 extra bits and goes down to
 * 8 bit with an ordered dither (no banding in the sky). The engine's own table is then the identity. */
typedef struct { uint16_t m[3][65536]; } fr_map;        /* 8.8 fixed point: 0..65280 */

static const uint8_t fr_bayer8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,   48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,   60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,   51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,   63, 31, 55, 23, 61, 29, 53, 21 };

/* the global tone sliders of lb_tone() without the exposure (that one is done in linear light already) */
static int fr_tone_rest(int y, const lb_adj *p)
{
    y = lb_clamp(y, 0, 4096);
    if (p->con) y += (p->con * (lb_smooth(y) - y)) / 100;
    if (p->hi) y += ((p->hi * lb_bell(y, 1434, 4096)) / 100 * 614) >> 12;
    if (p->sh) y += ((p->sh * lb_bell(y, 0, 2662)) / 100 * 614) >> 12;
    if (p->wh && y > 2458) { int t = ((y - 2458) << 12) / (4096 - 2458); y += ((p->wh * ((t * t) >> 12)) / 100 * 491) >> 12; }
    if (p->bl && y < 1638) { int t = ((1638 - y) << 12) / 1638; y += ((p->bl * ((t * t) >> 12)) / 100 * 491) >> 12; }
    return lb_clamp(y, 0, 4096);
}

/* ref: NULL, or 257 values (0..4096) = the tone of the camera's own JPEG as a function of the sRGB value of the linear light
 * in steps of 16 (see fd_raw_fit() in fd_core.h) */
static void fr_build_map(fr_map *mp, const film_t *f, const look_t *L, const int16_t *ref)
{
    int val[ATTR_COUNT];
    unsigned char has[ATTR_COUNT];
    int strength = lb_clamp(L->strength, 0, 100);
    lb_adj ad;
    lb_crv cv[4], uv[4];
    unsigned int wg[3] = { 65536u, 65536u, 65536u };
    unsigned int em = 65536u;
    int wb = (L->wb_temp != 0 || L->wb_tint != 0);
    int c, i, lift, liftq;

    fl_collect(val, has, f, L);
    ad.exp = lb_val(val, "Exposure2012", ""); ad.con = lb_val(val, "Contrast2012", "");
    ad.hi = lb_val(val, "Highlights2012", ""); ad.sh = lb_val(val, "Shadows2012", "");
    ad.wh = lb_val(val, "Whites2012", ""); ad.bl = lb_val(val, "Blacks2012", "");
    for (c = 0; c < 4; c++) { lb_crv_init(&cv[c], f, c, strength); lb_ucrv_init(&uv[c], L, c); }
    lift = lb_clamp(L->fade, 0, 100) * 60 / 100;
    liftq = (lift * 4096) / 255;
    if (wb) {
        int tt = lb_clamp(L->wb_temp, -100, 100), ti = lb_clamp(L->wb_tint, -100, 100);
        wg[0] = lb_gain((tt * 80) / 100);
        wg[2] = lb_gain(-((tt * 80) / 100));
        wg[1] = lb_gain(-((ti * 50) / 100));
    }
    if (ad.exp) {                                           /* 2^(exp / 100) in 16.16, as lb_tone() makes it */
        int e = ad.exp < 0 ? -ad.exp : ad.exp, whole = e / 100, rem = e % 100;
        int f8 = (rem * 8 * 256) / 100, k = f8 >> 8, fr = f8 & 255;
        if (whole > 5) whole = 5;
        em = lb_p2[k] + (((lb_p2[k + 1] - lb_p2[k]) * (unsigned)fr) >> 8);
        em <<= whole;
    }
    for (c = 0; c < 3; c++) {
        for (i = 0; i < 65536; i++) {
            uint64_t l = (uint64_t)i;
            int y;
            if (wb) { l = (l * wg[c]) >> 16; if (l > 65535u) l = 65535u; }
            if (ad.exp > 0) {                               /* more light: the highlights above the knee are rolled off, as in lb_tone() */
                const uint64_t knee = 45875u;
                l = (l * em) >> 16;
                if (l > 262000u) l = 262000u;
                if (l > knee) { uint64_t t = l - knee, r = 65535u - knee; l = knee + (r * t) / (t + r); }
            } else if (ad.exp < 0) l = (l << 16) / em;
            y = lb_l2s((int)l);                             /* 0..4096 */
            if (ref) { int b = y >> 4, fr = y & 15; y = b >= 256 ? ref[256] : ref[b] + (((ref[b + 1] - ref[b]) * fr) >> 4); }
            y = fr_tone_rest(y, &ad);
            if (cv[0].n) y = lb_crv_eval(&cv[0], y);
            if (uv[0].n) y = lb_crv_eval(&uv[0], y);
            if (lift) y = liftq + (y * (255 - lift)) / 255;
            if (cv[c + 1].n) y = lb_crv_eval(&cv[c + 1], y);
            if (uv[c + 1].n) y = lb_crv_eval(&uv[c + 1], y);
            y = lb_clamp(y, 0, 4096);
            mp->m[c][i] = (uint16_t)((y * 65280 + 2048) >> 12);
        }
    }
}

/* 16 bit linear rows -> 8 bit rows through the tables, with the dither (row y of the picture that is being made) */
static void fr_to8(const fr_map *mp, const uint16_t *in, int n, int y, uint8_t *out)
{
    const uint8_t *th = fr_bayer8 + ((y & 7) << 3);
    int x;
    for (x = 0; x < n; x++) {
        const int t = th[x & 7] * 4 + 2;
        int o = (mp->m[0][in[3 * x]] + t) >> 8;
        out[3 * x] = (uint8_t)(o > 255 ? 255 : o);
        o = (mp->m[1][in[3 * x + 1]] + t) >> 8;
        out[3 * x + 1] = (uint8_t)(o > 255 ? 255 : o);
        o = (mp->m[2][in[3 * x + 2]] + t) >> 8;
        out[3 * x + 2] = (uint8_t)(o > 255 ? 255 : o);
    }
}

#endif
