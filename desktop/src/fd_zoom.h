/* fd_zoom.h - FILM LOOK DESKTOP: a streaming, integer-only enlarger (bicubic, Catmull-Rom).
 *
 * The counterpart of fd_resize.h: that one makes pictures smaller (area average), this one makes them bigger, for the
 * "save in any resolution" of the program. Catmull-Rom is a cubic through the source pixels (a source pixel stays exactly
 * as it is where an output pixel sits on it), a little sharper than plain bicubic and without the soft look of a bilinear
 * enlargement. Separable: every source row is resized horizontally when it comes in, and a finished output row is the
 * weighted sum of four such rows.
 *
 * Nothing but integers is used (the weights are cubic polynomials in fixed point), so the result is the same on every computer
 * and in the browser. Streaming like the rest of the engine: rows go in once, in order; finished rows come out through a callback.
 * The core works on 16 bit values with a maximum (65535 for the RAW path, 255 for an 8 bit picture); fd_zs8 wraps it for 8 bit rows.
 *
 * Own code (not part of Soup Engine). */
#ifndef FD_ZOOM_H
#define FD_ZOOM_H

#include <stdint.h>
#include <stddef.h>

typedef int (*fd_zs_emit_fn)(void *ctx, const uint16_t *rgb, int nrows, int width);

typedef struct {
    int sw, sh, ow, oh, maxv;
    int *hidx;                   /* ow * 4: the four source columns of every output column (clamped to the picture) */
    int32_t *hw;                 /* ow * 4: their weights, 14 bit, adding up to 16384 */
    int *vmax;                   /* oh: the last source row that output row y needs */
    int *vidx;                   /* oh * 4: the four source rows (clamped) */
    int32_t *vw;                 /* oh * 4: their weights */
    int32_t *ring;               /* 4 rows of ow * 3: source rows resized horizontally, scaled by 2^14 */
    uint16_t *out;               /* ow * 3: one finished row */
    int y;                       /* next output row */
    int srow;                    /* source rows taken */
    fd_zs_emit_fn emit;
    void *ectx;
    int failed;
} fd_zs;

static void fd_zs_free(fd_zs *z)
{
    free(z->hidx); free(z->hw); free(z->vmax); free(z->vidx); free(z->vw); free(z->ring); free(z->out);
    z->hidx = 0; z->hw = 0; z->vmax = 0; z->vidx = 0; z->vw = 0; z->ring = 0; z->out = 0;
}

/* the four Catmull-Rom weights (14 bit, adding up to 16384) of an output position: i0 = the source pixel at or before it,
 * t = how far it is from there, 0..16383 (14 bit) */
static void fd_zs_weights(int t, int32_t w[4])
{
    int64_t t1 = t, t2 = (t1 * t1) >> 14, t3 = (t2 * t1) >> 14;
    int64_t a = -t3 + 2 * t2 - t1;                          /* x 1/2 */
    int64_t c = -3 * t3 + 4 * t2 + t1;
    int64_t d = t3 - t2;
    w[0] = (int32_t)(a >> 1);
    w[2] = (int32_t)(c >> 1);
    w[3] = (int32_t)(d >> 1);
    w[1] = (int32_t)(16384 - w[0] - w[2] - w[3]);           /* exactly 16384 in total (this is (3 t^3 - 5 t^2 + 2) / 2, give or take a rounding step) */
}

/* where the output pixel x of n (a picture of m pixels is stretched to n) looks into the source: the pixel i0 and t */
static void fd_zs_pos(int x, int n, int m, int *i0, int *t)
{
    /* the centre of output pixel x in source pixels: (x + 0.5) * m / n - 0.5, kept in units of 1 / (2 n) */
    int64_t u2 = (int64_t)(2 * (int64_t)x + 1) * m - n;
    int64_t den = 2 * (int64_t)n, fl;
    if (u2 >= 0) fl = u2 / den; else fl = -((-u2 + den - 1) / den);
    *i0 = (int)fl;
    *t = (int)(((u2 - fl * den) << 14) / den);
}

static int fd_zs_clampi(int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); }

/* 0 = ok, -1 = no memory / impossible sizes. The picture is only ever enlarged (ow >= sw, oh >= sh). */
static int fd_zs_begin(fd_zs *z, int sw, int sh, int ow, int oh, int maxv, fd_zs_emit_fn emit, void *ectx)
{
    int x, y;
    memset(z, 0, sizeof *z);
    if (sw < 1 || sh < 1 || ow < sw || oh < sh || ow > 60000 || oh > 60000 || sw > 60000 || sh > 60000) return -1;
    z->sw = sw; z->sh = sh; z->ow = ow; z->oh = oh; z->maxv = maxv; z->emit = emit; z->ectx = ectx;
    z->hidx = (int *)malloc(sizeof(int) * (size_t)ow * 4);
    z->hw = (int32_t *)malloc(sizeof(int32_t) * (size_t)ow * 4);
    z->vmax = (int *)malloc(sizeof(int) * (size_t)oh);
    z->vidx = (int *)malloc(sizeof(int) * (size_t)oh * 4);
    z->vw = (int32_t *)malloc(sizeof(int32_t) * (size_t)oh * 4);
    z->ring = (int32_t *)malloc(sizeof(int32_t) * (size_t)ow * 3 * 4);
    z->out = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)ow * 3);
    if (!z->hidx || !z->hw || !z->vmax || !z->vidx || !z->vw || !z->ring || !z->out) { fd_zs_free(z); return -1; }
    memset(z->ring, 0, sizeof(int32_t) * (size_t)ow * 3 * 4);
    for (x = 0; x < ow; x++) {
        int i0, t, k;
        fd_zs_pos(x, ow, sw, &i0, &t);
        fd_zs_weights(t, z->hw + (size_t)x * 4);
        for (k = 0; k < 4; k++) z->hidx[(size_t)x * 4 + (size_t)k] = fd_zs_clampi(i0 - 1 + k, sw - 1);
    }
    for (y = 0; y < oh; y++) {
        int i0, t, k;
        fd_zs_pos(y, oh, sh, &i0, &t);
        fd_zs_weights(t, z->vw + (size_t)y * 4);
        for (k = 0; k < 4; k++) z->vidx[(size_t)y * 4 + (size_t)k] = fd_zs_clampi(i0 - 1 + k, sh - 1);
        z->vmax[y] = fd_zs_clampi(i0 + 2, sh - 1);
    }
    return 0;
}

static int fd_zs_emit_row(fd_zs *z)
{
    const int n = z->ow * 3;
    const int32_t *w = z->vw + (size_t)z->y * 4;
    const int *vi = z->vidx + (size_t)z->y * 4;
    const int32_t *r0 = z->ring + (size_t)(vi[0] & 3) * (size_t)n, *r1 = z->ring + (size_t)(vi[1] & 3) * (size_t)n;
    const int32_t *r2 = z->ring + (size_t)(vi[2] & 3) * (size_t)n, *r3 = z->ring + (size_t)(vi[3] & 3) * (size_t)n;
    int i;
    for (i = 0; i < n; i++) {
        int64_t v = (int64_t)w[0] * r0[i] + (int64_t)w[1] * r1[i] + (int64_t)w[2] * r2[i] + (int64_t)w[3] * r3[i];
        v = (v + ((int64_t)1 << 27)) >> 28;
        z->out[i] = (uint16_t)(v < 0 ? 0 : (v > z->maxv ? z->maxv : v));
    }
    return z->emit(z->ectx, z->out, 1, z->ow);
}

/* the sink: nrows rows of interleaved RGB (16 bit values up to maxv), width = the source width */
static int fd_zs_rows(void *ctx, const uint16_t *rgb, int nrows, int width)
{
    fd_zs *z = (fd_zs *)ctx;
    int k;
    if (width != z->sw) { z->failed = 1; return -1; }
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        int x, c;
        int32_t *d;
        if (z->srow >= z->sh) break;                               /* more rows than announced: ignore them */
        d = z->ring + (size_t)(z->srow & 3) * (size_t)z->ow * 3;
        for (x = 0; x < z->ow; x++) {
            const int32_t *w = z->hw + (size_t)x * 4;
            const int *ix = z->hidx + (size_t)x * 4;
            for (c = 0; c < 3; c++)
                d[x * 3 + c] = w[0] * (int32_t)rgb[ix[0] * 3 + c] + w[1] * (int32_t)rgb[ix[1] * 3 + c]
                             + w[2] * (int32_t)rgb[ix[2] * 3 + c] + w[3] * (int32_t)rgb[ix[3] * 3 + c];
        }
        while (z->y < z->oh && z->vmax[z->y] <= z->srow) {
            if (fd_zs_emit_row(z) != 0) { z->failed = 1; return -1; }
            z->y++;
        }
        z->srow++;
    }
    return 0;
}

/* ---- 8 bit rows (the JPEG route): a thin wrapper that widens the rows going in and narrows the ones coming out */
typedef int (*fd_zs8_emit_fn)(void *ctx, const uint8_t *rgb, int nrows, int width);

typedef struct {
    fd_zs z;
    uint16_t *in;                /* sw * 3 */
    uint8_t *out;                /* ow * 3 */
    fd_zs8_emit_fn emit;
    void *ectx;
} fd_zs8;

static int fd_zs8_narrow(void *ctx, const uint16_t *rgb, int nrows, int width)
{
    fd_zs8 *a = (fd_zs8 *)ctx;
    int i, n = width * 3;
    (void)nrows;
    for (i = 0; i < n; i++) a->out[i] = (uint8_t)rgb[i];
    return a->emit(a->ectx, a->out, 1, width);
}

static void fd_zs8_free(fd_zs8 *a) { fd_zs_free(&a->z); free(a->in); free(a->out); a->in = 0; a->out = 0; }

static int fd_zs8_begin(fd_zs8 *a, int sw, int sh, int ow, int oh, fd_zs8_emit_fn emit, void *ectx)
{
    memset(a, 0, sizeof *a);
    a->emit = emit; a->ectx = ectx;
    if (sw < 1 || ow < 1) return -1;
    a->in = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)sw * 3);
    a->out = (uint8_t *)malloc((size_t)ow * 3);
    if (!a->in || !a->out) { fd_zs8_free(a); return -1; }
    if (fd_zs_begin(&a->z, sw, sh, ow, oh, 255, fd_zs8_narrow, a) != 0) { fd_zs8_free(a); return -1; }
    return 0;
}

static int fd_zs8_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fd_zs8 *a = (fd_zs8 *)ctx;
    int k, i;
    if (width != a->z.sw) { a->z.failed = 1; return -1; }
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        for (i = 0; i < width * 3; i++) a->in[i] = rgb[i];
        if (fd_zs_rows(&a->z, a->in, 1, width) != 0) return -1;
    }
    return 0;
}

#endif
