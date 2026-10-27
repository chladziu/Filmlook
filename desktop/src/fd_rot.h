/* fd_rot.h - FILM LOOK DESKTOP: straightening (turning the picture by a small angle) for the crop tool.
 *
 * Like the crop tool of Lightroom: the picture is turned by an angle round its centre, and the crop is a rectangle of the
 * (turned) picture that has to lie inside it. This stage sits between the source of the rows (the JPEG decoder, or the RAW
 * demosaic) and the resizer: it takes the rows of the picture as they come, keeps just the band of rows that the turned crop
 * needs, and gives out the rows of the crop rectangle, resampled with a cubic (Catmull-Rom, the same weights as fd_zoom.h).
 * The rows that come out have the size of the crop rectangle, 1 : 1 (no resizing happens here), so what comes after is the
 * very same code that works without a turn.
 *
 * Streaming and integer only, like the rest of the engine: sine and cosine come from a polynomial in fixed point (no libm,
 * which the browser build does not have either), positions are 64 bit fixed point, so a picture comes out byte for byte the same
 * on every computer and in the browser.
 *
 * Own code (not part of Soup Engine). */
#ifndef FD_ROT_H
#define FD_ROT_H

#include <stdint.h>
#include <stddef.h>

/* sine and cosine of an angle in thousandths of a degree (-90000 .. 90000), 30 bit fixed point */
static void fd_sincos(int mdeg, int64_t *sn, int64_t *cs)
{
    const int64_t ONE = (int64_t)1 << 30, PI30 = 3373259426LL;
    int neg = mdeg < 0, swapped = 0, k;
    int64_t x, x2, t, s, c;
    if (neg) mdeg = -mdeg;
    if (mdeg > 90000) mdeg = 90000;
    if (mdeg > 45000) { mdeg = 90000 - mdeg; swapped = 1; }
    x = ((int64_t)mdeg * PI30) / 180000;
    x2 = (x * x) >> 30;
    t = ONE;
    for (k = 11; k >= 3; k -= 2) t = ONE - (((x2 * t) >> 30) / (k * (k - 1)));
    s = (x * t) >> 30;
    t = ONE;
    for (k = 12; k >= 2; k -= 2) t = ONE - (((x2 * t) >> 30) / (k * (k - 1)));
    c = t;
    if (swapped) { int64_t u = s; s = c; c = u; }
    *sn = neg ? -s : s; *cs = c;
}

/* The biggest scale s (16 bit fixed point, 65536 = 1) at which a rectangle can be shrunk round its own centre so that all its
 * corners lie inside the picture turned by the angle (sn, cs). All lengths are in HALF pixels: the picture is dw x dh pixels,
 * the rectangle has its centre (mx2, my2) from the centre of the picture and its half sizes (hw2, hh2) -- that is, its width and
 * height in pixels. tol: how many pixels outside are still accepted. Returns 0 when the centre is outside already. */
static int fd_rot_fit(int64_t sn, int64_t cs, int dw, int dh, int mx2, int my2, int hw2, int hh2, int tol)
{
    int64_t best = 65536, lx = ((int64_t)(dw + 2 * tol)) << 30, ly = ((int64_t)(dh + 2 * tol)) << 30;
    int64_t ax = cs * mx2 + sn * my2, ay = -sn * mx2 + cs * my2;       /* the centre of the rectangle in the frame of the picture */
    int sx, sy, a;
    if ((ax < 0 ? -ax : ax) > lx || (ay < 0 ? -ay : ay) > ly) return 0;
    for (sx = -1; sx <= 1; sx += 2) for (sy = -1; sy <= 1; sy += 2) for (a = 0; a < 2; a++) {
        int64_t b = a ? (-sn * sx * hw2 + cs * sy * hh2) : (cs * sx * hw2 + sn * sy * hh2);
        int64_t A = a ? ay : ax, L = a ? ly : lx, room, s16;
        if (b == 0) continue;
        room = L - (b > 0 ? A : -A);                                    /* how far the corner may still go in its direction */
        if (b < 0) b = -b;
        s16 = (room << 16) / b;
        if (s16 < best) best = s16;
    }
    return (int)best;
}

/* ---- the stage */
typedef int (*fd_rot_emit_fn)(void *ctx, const uint16_t *rgb, int nrows, int width);

typedef struct {
    int bw, bh;                  /* the whole picture (as the source gives it) */
    int cw, ch;                  /* the crop rectangle = what comes out */
    int maxv;
    int64_t sn, cs;              /* the angle, 30 bit fixed point */
    int64_t x0q, y0q;            /* where output pixel (0, 0) looks into the picture, 31 bit fixed point, in pixel indices */
    int y0, y1;                  /* the first and the last row of the picture that is needed */
    int bx0, bx1;                /* the columns of the rows that are kept */
    int ky;                      /* the rows the ring holds */
    uint16_t *ring;              /* ky rows of (bx1 - bx0 + 1) * 3 values */
    uint16_t *out;               /* cw * 3: one finished row */
    int srow;                    /* the next row of the picture that is expected */
    int j;                       /* the next output row */
    fd_rot_emit_fn emit;
    void *ectx;
    int failed;
} fd_rot;

static void fd_rot_free(fd_rot *r) { free(r->ring); free(r->out); r->ring = 0; r->out = 0; }

static int64_t fd_rot_ymax31(const fd_rot *r, int j)      /* the lowest source position of row j, 31 bit */
{
    int64_t y = r->y0q + (int64_t)j * 2 * r->cs;
    return r->sn >= 0 ? y : y - (int64_t)(r->cw - 1) * 2 * r->sn;
}
static int64_t fd_rot_ymin31(const fd_rot *r, int j)
{
    int64_t y = r->y0q + (int64_t)j * 2 * r->cs;
    return r->sn >= 0 ? y - (int64_t)(r->cw - 1) * 2 * r->sn : y;
}
static int fd_rot_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int fd_rot_lastrow(const fd_rot *r, int j) { return fd_rot_clampi((int)(fd_rot_ymax31(r, j) >> 31) + 2, 0, r->bh - 1); }
static int fd_rot_firstrow(const fd_rot *r, int j) { return fd_rot_clampi((int)(fd_rot_ymin31(r, j) >> 31) - 1, 0, r->bh - 1); }

/* The crop rectangle is cx, cy, cw, ch in the pixels of the picture (bw x bh); the picture is turned by mdeg thousandths of a
 * degree round its centre, clockwise when positive. maxv: 255 or 65535. 0 = ok, -1 = no memory or impossible sizes. */
static int fd_rot_begin(fd_rot *r, int bw, int bh, int cx, int cy, int cw, int ch, int mdeg, int maxv, fd_rot_emit_fn emit, void *ectx)
{
    int64_t dx2, dy2, xs[4], ys[4], xmin, xmax, ymin, ymax;
    int j, i, need;
    memset(r, 0, sizeof *r);
    if (bw < 1 || bh < 1 || cw < 1 || ch < 1 || bw > 60000 || bh > 60000 || cw > 60000 || ch > 60000) return -1;
    r->bw = bw; r->bh = bh; r->cw = cw; r->ch = ch; r->maxv = maxv; r->emit = emit; r->ectx = ectx;
    fd_sincos(mdeg, &r->sn, &r->cs);
    dx2 = 2 * (int64_t)cx + 1 - bw; dy2 = 2 * (int64_t)cy + 1 - bh;        /* the first output pixel from the centre, in half pixels */
    r->x0q = ((int64_t)(bw - 1) << 30) + r->cs * dx2 + r->sn * dy2;
    r->y0q = ((int64_t)(bh - 1) << 30) - r->sn * dx2 + r->cs * dy2;
    for (i = 0; i < 4; i++) {                                               /* the corners of the crop: where they look into the picture */
        int ci = (i & 1) ? cw - 1 : 0, cj = (i & 2) ? ch - 1 : 0;
        xs[i] = r->x0q + (int64_t)ci * 2 * r->cs + (int64_t)cj * 2 * r->sn;
        ys[i] = r->y0q - (int64_t)ci * 2 * r->sn + (int64_t)cj * 2 * r->cs;
    }
    xmin = xmax = xs[0]; ymin = ymax = ys[0];
    for (i = 1; i < 4; i++) {
        if (xs[i] < xmin) xmin = xs[i];
        if (xs[i] > xmax) xmax = xs[i];
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    r->bx0 = fd_rot_clampi((int)(xmin >> 31) - 2, 0, bw - 1); r->bx1 = fd_rot_clampi((int)(xmax >> 31) + 3, 0, bw - 1);
    r->y0 = fd_rot_clampi((int)(ymin >> 31) - 2, 0, bh - 1);   r->y1 = fd_rot_clampi((int)(ymax >> 31) + 3, 0, bh - 1);
    need = 1;
    for (j = 0; j < ch; j++) {                                              /* the most rows that one output row reaches over */
        int n = fd_rot_lastrow(r, j) - fd_rot_firstrow(r, j) + 1;
        if (n > need) need = n;
    }
    r->ky = need + 1;
    r->ring = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)r->ky * (size_t)(r->bx1 - r->bx0 + 1) * 3);
    r->out = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)cw * 3);
    if (!r->ring || !r->out) { fd_rot_free(r); return -1; }
    r->srow = r->y0;
    return 0;
}

static int fd_rot_emit_row(fd_rot *r)
{
    const int j = r->j, kw = r->bx1 - r->bx0 + 1;
    const int64_t sn2 = 2 * r->sn, cs2 = 2 * r->cs;
    int64_t xq = r->x0q + (int64_t)j * sn2, yq = r->y0q + (int64_t)j * cs2;
    int i, c, k, l;
    for (i = 0; i < r->cw; i++, xq += cs2, yq -= sn2) {
        int ix = (int)(xq >> 31), iy = (int)(yq >> 31);
        int32_t wx[4], wy[4];
        const uint16_t *rowp[4];
        int cxi[4];
        fd_zs_weights((int)((xq >> 17) & 16383), wx);
        fd_zs_weights((int)((yq >> 17) & 16383), wy);
        for (k = 0; k < 4; k++) {
            int yy = fd_rot_clampi(fd_rot_clampi(iy - 1 + k, 0, r->bh - 1), r->y0, r->y1);
            rowp[k] = r->ring + (size_t)(yy % r->ky) * (size_t)kw * 3;
            cxi[k] = fd_rot_clampi(fd_rot_clampi(ix - 1 + k, 0, r->bw - 1), r->bx0, r->bx1) - r->bx0;
        }
        for (c = 0; c < 3; c++) {
            int64_t v = 0;
            for (l = 0; l < 4; l++) {
                const uint16_t *p = rowp[l];
                int32_t h = wx[0] * (int32_t)p[cxi[0] * 3 + c] + wx[1] * (int32_t)p[cxi[1] * 3 + c]
                          + wx[2] * (int32_t)p[cxi[2] * 3 + c] + wx[3] * (int32_t)p[cxi[3] * 3 + c];
                v += (int64_t)wy[l] * h;
            }
            v = (v + ((int64_t)1 << 27)) >> 28;
            r->out[i * 3 + c] = (uint16_t)(v < 0 ? 0 : (v > r->maxv ? r->maxv : v));
        }
    }
    return r->emit(r->ectx, r->out, 1, r->cw);
}

/* the sink: nrows rows of the picture (bw pixels wide), the first of them is row r->y0, the next ones follow without gaps */
static int fd_rot_rows(void *ctx, const uint16_t *rgb, int nrows, int width)
{
    fd_rot *r = (fd_rot *)ctx;
    const int kw = r->bx1 - r->bx0 + 1;
    int k;
    if (width != r->bw) { r->failed = 1; return -1; }
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        int y = r->srow;
        if (y > r->y1) break;                                              /* more rows than needed: ignore them */
        memcpy(r->ring + (size_t)(y % r->ky) * (size_t)kw * 3, rgb + (size_t)r->bx0 * 3, sizeof(uint16_t) * (size_t)kw * 3);
        r->srow++;
        while (r->j < r->ch && fd_rot_lastrow(r, r->j) <= y) {
            if (fd_rot_emit_row(r) != 0) { r->failed = 1; return -1; }
            r->j++;
        }
    }
    return 0;
}

/* 1 when every row of the crop has come out */
static int fd_rot_done(const fd_rot *r) { return r->j >= r->ch; }

/* ---- 8 bit rows (the JPEG route) */
typedef int (*fd_rot8_emit_fn)(void *ctx, const uint8_t *rgb, int nrows, int width);

typedef struct {
    fd_rot r;
    uint16_t *in;                /* bw * 3 */
    uint8_t *out;                /* cw * 3 */
    fd_rot8_emit_fn emit;
    void *ectx;
} fd_rot8;

static int fd_rot8_narrow(void *ctx, const uint16_t *rgb, int nrows, int width)
{
    fd_rot8 *a = (fd_rot8 *)ctx;
    int i, n = width * 3;
    (void)nrows;
    for (i = 0; i < n; i++) a->out[i] = (uint8_t)rgb[i];
    return a->emit(a->ectx, a->out, 1, width);
}

static void fd_rot8_free(fd_rot8 *a) { fd_rot_free(&a->r); free(a->in); free(a->out); a->in = 0; a->out = 0; }

static int fd_rot8_begin(fd_rot8 *a, int bw, int bh, int cx, int cy, int cw, int ch, int mdeg, fd_rot8_emit_fn emit, void *ectx)
{
    memset(a, 0, sizeof *a);
    a->emit = emit; a->ectx = ectx;
    a->in = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)bw * 3);
    a->out = (uint8_t *)malloc((size_t)cw * 3);
    if (!a->in || !a->out) { fd_rot8_free(a); return -1; }
    if (fd_rot_begin(&a->r, bw, bh, cx, cy, cw, ch, mdeg, 255, fd_rot8_narrow, a) != 0) { fd_rot8_free(a); return -1; }
    return 0;
}

static int fd_rot8_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fd_rot8 *a = (fd_rot8 *)ctx;
    int k, i;
    if (width != a->r.bw) { a->r.failed = 1; return -1; }
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        for (i = a->r.bx0 * 3; i < (a->r.bx1 + 1) * 3; i++) a->in[i] = rgb[i];
        if (fd_rot_rows(&a->r, a->in, 1, width) != 0) return -1;
    }
    return 0;
}

#endif
