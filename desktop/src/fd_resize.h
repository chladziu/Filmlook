/* fd_resize.h - FILM LOOK DESKTOP: a streaming, integer-only "area average" resizer.
 *
 * It sits between the JPEG decoder and the film look when the picture has to come out smaller than it went in
 * (the sizes 3861x2574 and 2784x1856 of the 5D Mark II). Every output pixel is the exact average of the part of the
 * source it covers, so the picture is not shifted and not sharpened; nothing but integers is used, so the result is
 * the same on every computer (and in the browser).
 *
 * Streaming like everything in the engine: source rows go in once, in order, in blocks of any size; finished output
 * rows come out through the same callback type as the decoder's, so the film look sees them like decoder rows.
 *
 * Own code (not part of Soup Engine). */
#ifndef FD_RESIZE_H
#define FD_RESIZE_H

#include <stdint.h>
#include <stddef.h>

typedef int (*fd_rs_emit_fn)(void *ctx, const uint8_t *rgb, int nrows, int width);

typedef struct {
    int sw, sh, ow, oh;
    int span;                    /* most source pixels that one output pixel covers */
    int *first, *cnt;            /* per output column: first source pixel, how many */
    uint32_t *wt;                /* ow * span weights, they add up to sw for every column */
    uint32_t *hrow;              /* ow * 3: the source row, resized horizontally (value * sw) */
    uint64_t *acc;               /* ow * 3: the output row being summed up */
    uint8_t *out;                /* ow * 3: one finished output row */
    int64_t pos;                 /* vertical position of the next source row, in units of 1 / (sh * oh) of the picture */
    int y;                       /* output row being summed up */
    int srow;                    /* source rows taken */
    fd_rs_emit_fn emit;
    void *ectx;
    int failed;
} fd_rs;

static void fd_rs_free(fd_rs *r)
{
    free(r->first); free(r->cnt); free(r->wt); free(r->hrow); free(r->acc); free(r->out);
    r->first = 0; r->cnt = 0; r->wt = 0; r->hrow = 0; r->acc = 0; r->out = 0;
}

/* 0 = ok, -1 = no memory / impossible sizes */
static int fd_rs_begin(fd_rs *r, int sw, int sh, int ow, int oh, fd_rs_emit_fn emit, void *ectx)
{
    int x;
    memset(r, 0, sizeof *r);
    if (sw < 1 || sh < 1 || ow < 1 || oh < 1 || ow > sw || oh > sh || sw > 60000 || sh > 60000) return -1;
    r->sw = sw; r->sh = sh; r->ow = ow; r->oh = oh; r->emit = emit; r->ectx = ectx;
    r->span = (sw + ow - 1) / ow + 1;
    r->first = (int *)malloc(sizeof(int) * (size_t)ow);
    r->cnt = (int *)malloc(sizeof(int) * (size_t)ow);
    r->wt = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)ow * (size_t)r->span);
    r->hrow = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)ow * 3);
    r->acc = (uint64_t *)malloc(sizeof(uint64_t) * (size_t)ow * 3);
    r->out = (uint8_t *)malloc((size_t)ow * 3);
    if (!r->first || !r->cnt || !r->wt || !r->hrow || !r->acc || !r->out) { fd_rs_free(r); return -1; }
    memset(r->acc, 0, sizeof(uint64_t) * (size_t)ow * 3);
    /* source pixel i covers [i*ow, (i+1)*ow), output pixel x covers [x*sw, (x+1)*sw), both in units of 1/(sw*ow) */
    for (x = 0; x < ow; x++) {
        int64_t a = (int64_t)x * sw, b = a + sw;
        int i = (int)(a / ow), n = 0;
        r->first[x] = i;
        while ((int64_t)i * ow < b && i < sw) {
            int64_t lo = (int64_t)i * ow > a ? (int64_t)i * ow : a;
            int64_t hi = (int64_t)(i + 1) * ow < b ? (int64_t)(i + 1) * ow : b;
            r->wt[(size_t)x * (size_t)r->span + (size_t)n] = (uint32_t)(hi - lo);
            n++; i++;
        }
        r->cnt[x] = n;
    }
    return 0;
}

static int fd_rs_emit_row(fd_rs *r)
{
    int i, n = r->ow * 3;
    uint64_t norm = (uint64_t)r->sw * (uint64_t)r->sh, half = norm / 2;
    for (i = 0; i < n; i++) {
        uint64_t v = (r->acc[i] + half) / norm;
        r->out[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    return r->emit(r->ectx, r->out, 1, r->ow);
}

/* the decoder's sink: nrows rows of interleaved RGB, width = the source width */
static int fd_rs_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fd_rs *r = (fd_rs *)ctx;
    int k;
    if (width != r->sw) { r->failed = 1; return -1; }
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        int x, c;
        int64_t end, pos;
        if (r->srow >= r->sh) break;                               /* more rows than announced: ignore them */
        /* horizontal: every output pixel is the weighted sum of its source pixels (weights add up to sw) */
        for (x = 0; x < r->ow; x++) {
            const uint32_t *w = r->wt + (size_t)x * (size_t)r->span;
            const uint8_t *s = rgb + (size_t)r->first[x] * 3;
            uint32_t s0 = 0, s1 = 0, s2 = 0;
            int n = r->cnt[x], i;
            for (i = 0; i < n; i++, s += 3) { s0 += w[i] * s[0]; s1 += w[i] * s[1]; s2 += w[i] * s[2]; }
            r->hrow[x * 3] = s0; r->hrow[x * 3 + 1] = s1; r->hrow[x * 3 + 2] = s2;
        }
        /* vertical: source row j covers [j*oh, (j+1)*oh), output row y covers [y*sh, (y+1)*sh), in units of 1/(sh*oh) */
        pos = (int64_t)r->srow * r->oh;
        end = pos + r->oh;
        while (pos < end) {
            int64_t ybound = (int64_t)(r->y + 1) * r->sh;
            int64_t stop = end < ybound ? end : ybound;
            uint64_t seg = (uint64_t)(stop - pos);
            uint64_t *a = r->acc;
            int n = r->ow * 3;
            for (c = 0; c < n; c++) a[c] += seg * r->hrow[c];
            pos = stop;
            if (pos == ybound) {                                    /* this output row is complete */
                if (fd_rs_emit_row(r) != 0) { r->failed = 1; return -1; }
                memset(r->acc, 0, sizeof(uint64_t) * (size_t)n);
                r->y++;
                if (r->y >= r->oh) { r->srow++; return 0; }
            }
            /* a source row reaches into at most two output rows when shrinking (oh <= sh): the rest of it goes into the
             * new row in the next turn of this loop */
        }
        r->srow++;
    }
    return 0;
}

#endif
