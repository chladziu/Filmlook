/*
 * viewer.h - the viewer of the finished pictures (tab "Zdjęcia" of the panel). Pure C without camera calls, so the PC test
 * (tools_host/viewtest.c) runs the very same code that the camera runs.
 *
 *   - the list of the pictures (names of *.JPG, the newest last),
 *   - the zoom levels and where a picture goes on the screen,
 *   - the tile: a part of the picture, decoded at 1/8, 1/4, 1/2 or full size by the window decoder (engine/jpeg_dec.c) and kept in RAM, so
 *     that moving about inside it needs no new decoding,
 *   - the picture on the screen: the 8-bit layer of the panel (so it shows on every screen mode of the camera, exactly like the panel does)
 *     with a palette of 231 colours made for this very picture (median cut) and error diffusion (Floyd-Steinberg),
 *   - the strips above and below the picture (name, zoom, number, keys) and the small badge when the picture fills the whole screen.
 *
 * The screen is 720 x 480 numbers, but the LCD of the 5D II has 640 x 480 pixels: a buffer pixel is 8/9 of a screen pixel wide. A picture
 * of 3:2 therefore fills the whole width and 427 of the 480 rows (a strip of 26 rows above and below). The zoom shows a part of
 * 640 x 480 decoded pixels, 1 decoded pixel = 1 screen pixel, stretched to the 720 columns.
 */
#ifndef VIEWER_H
#define VIEWER_H

#include <string.h>
#include "engine/jpeg_dec.h"

#define VW_W      720
#define VW_H      480
#define VW_PHW    640                  /* width of the LCD in pixels */
#define VW_COLS   231                  /* palette entries 1 .. 231 belong to the picture, PAL_BASE (232) and up to the panel's own colours */
#define VW_TMAX   2048                 /* widest part of a picture that is ever sampled */
#define VW_BAR_T  26                   /* the strip above a 3:2 picture */
#define VW_BAR_B  26

/* ------------------------------------------------------------------ the list of the pictures */
#define VW_MAXPH  1024
typedef struct { char name[13]; unsigned char dir; int key; } vw_ph;          /* dir: 0 = FILMLOOK, 1 = another folder; key: the number in the name */
static vw_ph vw_list[VW_MAXPH];
static int vw_n = 0;

/* the number in IMG_1234.JPG (the four digits in front of the dot); 0 for a name of another shape. The list is in the order of these
 * numbers: the same order the camera counts in, whatever the first letters (IMG_ / _MG_) are */
static int vw_key(const char *name)
{
    int l = (int)strlen(name), i, v = 0;
    if (l < 8 || name[l - 4] != '.') return 0;
    for (i = l - 8; i < l - 4; i++) { if (name[i] < '0' || name[i] > '9') return 0; v = v * 10 + (name[i] - '0'); }
    return v;
}

static int vw_cmp(const vw_ph *a, const vw_ph *b)
{
    int ka = a->key, kb = b->key, c;
    if (ka != kb) return ka < kb ? -1 : 1;
    c = strcmp(a->name, b->name);
    if (c) return c;
    return (int)a->dir - (int)b->dir;
}

static int vw_is_jpg(const char *n)
{
    int l = (int)strlen(n);
    if (l < 5 || l > 12 || n[0] == '.') return 0;
    return n[l - 4] == '.' && (n[l - 3] == 'J' || n[l - 3] == 'j') && (n[l - 2] == 'P' || n[l - 2] == 'p') && (n[l - 1] == 'G' || n[l - 1] == 'g');
}

/* adds a file of a folder listing (not sorted yet; a listing has every name once): when the list is full the oldest one goes.
 * Returns 1 when it was added */
static int vw_list_add(const char *name, int dir)
{
    vw_ph w;
    int i, lo = 0;
    if (!vw_is_jpg(name)) return 0;
    memset(&w, 0, sizeof w);
    strcpy(w.name, name);
    w.dir = (unsigned char)dir;
    w.key = vw_key(name);
    if (vw_n < VW_MAXPH) { vw_list[vw_n++] = w; return 1; }
    for (i = 1; i < vw_n; i++) if (vw_cmp(&vw_list[i], &vw_list[lo]) < 0) lo = i;
    if (vw_cmp(&w, &vw_list[lo]) > 0) { vw_list[lo] = w; return 1; }
    return 0;
}

static void vw_list_sort(void)                      /* insertion sort: the card lists the files nearly in order */
{
    int i, j;
    for (i = 1; i < vw_n; i++) {
        vw_ph w = vw_list[i];
        for (j = i - 1; j >= 0 && vw_cmp(&vw_list[j], &w) > 0; j--) vw_list[j + 1] = vw_list[j];
        vw_list[j + 1] = w;
    }
}

/* ------------------------------------------------------------------ zoom levels and layout */
typedef struct {
    int W, H;              /* the picture as it is coded */
    int dfit;              /* the picture is decoded at 1/dfit for the whole-picture view: 8, 4, 2 or 1 */
    int nlev;              /* number of zoom levels: dfit 8 -> 1/8, 1/4, 1/2, 1/1 */
} vw_pic;

static void vw_pic_set(vw_pic *p, int W, int H)
{
    int d;
    p->W = W; p->H = H;
    for (d = 8; d > 1; d >>= 1) if (d * VW_PHW <= W || d * 480 <= H) break;        /* the largest reduction that still has a pixel for every pixel of the screen */
    p->dfit = d;
    p->nlev = d == 8 ? 4 : d == 4 ? 3 : d == 2 ? 2 : 1;
}

typedef struct {
    int d;                 /* denominator */
    int iw, ih;            /* the whole picture at 1/d */
    int sx, sy, sw, sh;    /* the part that is shown (pixels at 1/d) */
    int dx, dy, dw, dh;    /* where it goes on the screen (buffer pixels, dx and dw even) */
} vw_lay;

static int vw_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* cx, cy: the centre of the part that is shown, in pixels of the coded picture (they are kept inside the picture) */
static void vw_layout(const vw_pic *p, int lev, int *cx, int *cy, vw_lay *L)
{
    int d = p->dfit >> lev;
    se_jpeg_scaled_size(p->W, p->H, d, &L->iw, &L->ih);
    if (lev == 0) {
        int pw, ph;
        L->sx = L->sy = 0; L->sw = L->iw; L->sh = L->ih;
        if ((long)L->iw * 480 >= (long)L->ih * VW_PHW) { pw = VW_PHW; ph = (L->ih * VW_PHW + L->iw / 2) / L->iw; }
        else                                            { ph = 480;    pw = (L->iw * 480 + L->ih / 2) / L->ih; }
        L->dw = pw * 9 / 8; L->dh = ph;
        *cx = p->W / 2; *cy = p->H / 2;
    } else {
        L->sw = L->iw < VW_PHW ? L->iw : VW_PHW;
        L->sh = L->ih < 480 ? L->ih : 480;
        L->sx = vw_clamp(*cx / d - L->sw / 2, 0, L->iw - L->sw);
        L->sy = vw_clamp(*cy / d - L->sh / 2, 0, L->ih - L->sh);
        *cx = (L->sx + L->sw / 2) * d; *cy = (L->sy + L->sh / 2) * d;
        L->dw = L->sw * 9 / 8; L->dh = L->sh;
    }
    L->dw &= ~1;
    if (L->dw > VW_W) L->dw = VW_W;
    if (L->dh > VW_H) L->dh = VW_H;
    if (L->dw < 2) L->dw = 2;
    if (L->dh < 1) L->dh = 1;
    L->d = d;
    L->dx = ((VW_W - L->dw) / 2) & ~1;
    L->dy = (VW_H - L->dh) / 2;
}

/* the size of the picture on the screen as a percentage of its pixels: the whole-picture view shows 11 % of a 5616 px picture, the
 * zoom levels show 25 %, 50 % and 100 % (1 decoded pixel = 1 screen pixel) */
static int vw_percent(const vw_pic *p, const vw_lay *L, int lev)
{
    if (lev == 0) return (L->dw * 8 / 9 * 100 + p->W / 2) / p->W;
    return 100 / L->d;
}

/* ------------------------------------------------------------------ tiles */
typedef struct {
    uint8_t *rgb;          /* w x h pixels, 3 bytes each */
    size_t cap;            /* bytes that rgb can hold */
    int ok, d, x0, y0, w, h;
} vw_tile;

/* the tile for a layout: the whole picture when it fits, else the shown part with as much around it as fits */
static int vw_tile_plan(const vw_lay *L, size_t cap, int *tx, int *ty, int *tw, int *th)
{
    size_t px = cap / 3;
    int w = L->iw, h = L->ih;
    if ((size_t)w * (size_t)h > px) {
        w = L->iw < 1280 ? L->iw : 1280;
        if (w < L->sw) w = L->sw;
        h = (int)(px / (size_t)w);
        if (h > L->ih) h = L->ih;
        if (h < L->sh) { w = L->sw; h = (int)(px / (size_t)w); if (h > L->ih) h = L->ih; if (h < L->sh) return 0; }
    }
    *tw = w; *th = h;
    *tx = vw_clamp(L->sx + L->sw / 2 - w / 2, 0, L->iw - w);
    *ty = vw_clamp(L->sy + L->sh / 2 - h / 2, 0, L->ih - h);
    return 1;
}

static int vw_tile_has(const vw_tile *t, const vw_lay *L)
{
    return t->ok && t->d == L->d && L->sx >= t->x0 && L->sx + L->sw <= t->x0 + t->w && L->sy >= t->y0 && L->sy + L->sh <= t->y0 + t->h;
}

/* where the bytes of a picture come from */
typedef struct {
    int (*read)(void *ctx, uint32_t off, void *dst, uint32_t n);
    void *rctx;
    uint32_t size;
    const uint8_t *hdr;    /* the first bytes of the file (the markers before the picture data) */
    uint32_t hdrlen;
    se_jdec_stop_fn stop;  /* asked once per row of blocks: non-zero ends the decoding with SE_E_ABORT */
    void *sctx;
} vw_src;

typedef struct { uint8_t *dst; int w, h, got; } vw_sinkctx;

static int vw_sink(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    vw_sinkctx *c = (vw_sinkctx *)ctx;
    if (width != c->w || c->got + nrows > c->h) return 1;
    memcpy(c->dst + (size_t)c->got * (size_t)width * 3, rgb, (size_t)nrows * (size_t)width * 3);
    c->got += nrows;
    return 0;
}

static se_err vw_decode_tile(const vw_src *s, int d, vw_tile *t, int x0, int y0, int w, int h, se_arena *ar)
{
    se_jdec_info ji;
    vw_sinkctx c;
    se_err rc;
    t->ok = 0;
    if ((size_t)w * (size_t)h * 3 > t->cap) return SE_E_NOMEM;
    se_arena_reset(ar);
    c.dst = t->rgb; c.w = w; c.h = h; c.got = 0;
    rc = se_jpeg_decode_window(s->hdr, s->hdrlen, s->size, s->read, s->rctx, d, x0, y0, w, h, s->stop, s->sctx, ar, vw_sink, &c, &ji);
    if (rc == SE_OK && c.got != h) rc = SE_E_TRUNCATED;
    if (rc == SE_OK) { t->ok = 1; t->d = d; t->x0 = x0; t->y0 = y0; t->w = w; t->h = h; }
    return rc;
}

/* ------------------------------------------------------------------ one row of the screen from the tile */
static uint8_t vw_tmp[VW_TMAX * 3];

/* row Y (0 .. dh - 1) of the layout as dw pixels of RGB: bilinear, first between two rows of the tile, then along the row */
static void vw_sample_row(const vw_tile *t, const vw_lay *L, int Y, uint8_t *out)
{
    int stepy = (L->sh << 16) / L->dh, stepx = (L->sw << 16) / L->dw;
    int v = Y * stepy + stepy / 2 - 0x8000, r0, fy, i, X, n = L->sw * 3;
    const uint8_t *row0, *row1, *src;
    if (v < 0) v = 0;
    r0 = v >> 16; fy = (v >> 8) & 255;
    if (r0 >= L->sh - 1) { r0 = L->sh - 1; fy = 0; }
    row0 = t->rgb + ((size_t)(L->sy + r0 - t->y0) * (size_t)t->w + (size_t)(L->sx - t->x0)) * 3;
    if (fy == 0) src = row0;
    else {
        row1 = row0 + (size_t)t->w * 3;
        for (i = 0; i < n; i++) vw_tmp[i] = (uint8_t)((row0[i] * (256 - fy) + row1[i] * fy) >> 8);
        src = vw_tmp;
    }
    for (X = 0; X < L->dw; X++) {
        int u = X * stepx + stepx / 2 - 0x8000, c0, c1, fx;
        const uint8_t *a, *b;
        if (u < 0) u = 0;
        c0 = u >> 16; fx = (u >> 8) & 255;
        if (c0 >= L->sw - 1) { c0 = L->sw - 1; fx = 0; }
        c1 = c0 + 1 < L->sw ? c0 + 1 : c0;
        a = src + c0 * 3; b = src + c1 * 3;
        out[X * 3 + 0] = (uint8_t)((a[0] * (256 - fx) + b[0] * fx) >> 8);
        out[X * 3 + 1] = (uint8_t)((a[1] * (256 - fx) + b[1] * fx) >> 8);
        out[X * 3 + 2] = (uint8_t)((a[2] * (256 - fx) + b[2] * fx) >> 8);
    }
}

/* ------------------------------------------------------------------ palette: median cut + error diffusion */
#define HIDX(r, g, b) (((r) << 10) | ((g) << 5) | (b))
static uint32_t *vw_hist;                      /* 32768 cells (5 bits per colour): the number of pixels of this colour */
static uint8_t  *vw_inv;                       /* 32768 cells: the palette entry (1 ..) the colour is drawn with */
typedef struct { uint8_t lo[3], hi[3], dead; uint32_t n; } vw_box;
static vw_box vw_bx[VW_COLS];
static int vw_nbx;
static uint8_t vw_prgb[VW_COLS][3];            /* the colours of the palette entries 1 .. vw_nbx */

static void vw_hist_make(const vw_tile *t, const vw_lay *L)
{
    int y, x, ys = 1, xs = 1;
    memset(vw_hist, 0, 32768 * sizeof(uint32_t));
    if (L->sw * L->sh > 450000) { xs = 2; ys = 2; }                      /* a big part: every other pixel is enough for the colours */
    for (y = 0; y < L->sh; y += ys) {
        const uint8_t *p = t->rgb + ((size_t)(L->sy + y - t->y0) * (size_t)t->w + (size_t)(L->sx - t->x0)) * 3;
        for (x = 0; x < L->sw; x += xs, p += 3 * xs) vw_hist[HIDX(p[0] >> 3, p[1] >> 3, p[2] >> 3)]++;
    }
}

/* splits box bi in two where the pixels are halved; 0 when it can not be split (a single colour) */
static int vw_split(int bi)
{
    static const int wt[3] = { 4, 5, 3 };                                /* the eye cares most about green, then red */
    vw_box *b = &vw_bx[bi];
    int mn[3] = { 31, 31, 31 }, mx[3] = { 0, 0, 0 }, r, g, bl, a, best = -1, bestv = 0, m, v;
    uint32_t cnt[32], half, cum = 0, left = 0;
    for (r = b->lo[0]; r <= b->hi[0]; r++)
        for (g = b->lo[1]; g <= b->hi[1]; g++)
            for (bl = b->lo[2]; bl <= b->hi[2]; bl++)
                if (vw_hist[HIDX(r, g, bl)]) {
                    if (r < mn[0]) mn[0] = r;   if (r > mx[0]) mx[0] = r;
                    if (g < mn[1]) mn[1] = g;   if (g > mx[1]) mx[1] = g;
                    if (bl < mn[2]) mn[2] = bl; if (bl > mx[2]) mx[2] = bl;
                }
    for (a = 0; a < 3; a++) if (mx[a] > mn[a] && (mx[a] - mn[a]) * wt[a] > bestv) { bestv = (mx[a] - mn[a]) * wt[a]; best = a; }
    if (best < 0) return 0;
    memset(cnt, 0, sizeof cnt);
    for (r = b->lo[0]; r <= b->hi[0]; r++)
        for (g = b->lo[1]; g <= b->hi[1]; g++)
            for (bl = b->lo[2]; bl <= b->hi[2]; bl++) {
                uint32_t c = vw_hist[HIDX(r, g, bl)];
                if (c) cnt[best == 0 ? r : (best == 1 ? g : bl)] += c;
            }
    half = b->n / 2;
    m = mn[best];
    for (v = mn[best]; v <= mx[best]; v++) { cum += cnt[v]; m = v; if (cum >= half) break; }
    if (m >= mx[best]) m = mx[best] - 1;
    for (v = mn[best]; v <= m; v++) left += cnt[v];
    vw_bx[vw_nbx] = *b;                                    /* the new box gets the upper part, with the pixels that are left */
    vw_bx[vw_nbx].lo[best] = (uint8_t)(m + 1);
    vw_bx[vw_nbx].n = b->n - left;
    b->hi[best] = (uint8_t)m;
    b->n = left;
    vw_nbx++;
    return 1;
}

/* the palette and the table colour -> entry, from the histogram */
static void vw_palette_make(void)
{
    uint32_t tot = 0;
    int i, r, g, b;
    for (i = 0; i < 32768; i++) tot += vw_hist[i];
    memset(vw_bx, 0, sizeof vw_bx);
    for (i = 0; i < 3; i++) { vw_bx[0].lo[i] = 0; vw_bx[0].hi[i] = 31; }
    vw_bx[0].n = tot;
    vw_nbx = 1;
    while (vw_nbx < VW_COLS) {
        int bi = -1;
        uint32_t best = 1;
        for (i = 0; i < vw_nbx; i++) if (!vw_bx[i].dead && vw_bx[i].n > best) { best = vw_bx[i].n; bi = i; }
        if (bi < 0) break;
        if (!vw_split(bi)) vw_bx[bi].dead = 1;
    }
    for (i = 0; i < vw_nbx; i++) {                                       /* the colour of an entry: the mean of the pixels in its box */
        const vw_box *bx = &vw_bx[i];
        uint32_t rs = 0, gs = 0, bs = 0, n = 0;
        for (r = bx->lo[0]; r <= bx->hi[0]; r++)
            for (g = bx->lo[1]; g <= bx->hi[1]; g++)
                for (b = bx->lo[2]; b <= bx->hi[2]; b++) {
                    uint32_t c = vw_hist[HIDX(r, g, b)];
                    vw_inv[HIDX(r, g, b)] = (uint8_t)(i + 1);
                    if (c) { rs += c * (uint32_t)((r << 3) | (r >> 2)); gs += c * (uint32_t)((g << 3) | (g >> 2)); bs += c * (uint32_t)((b << 3) | (b >> 2)); n += c; }
                }
        if (n) { vw_prgb[i][0] = (uint8_t)(rs / n); vw_prgb[i][1] = (uint8_t)(gs / n); vw_prgb[i][2] = (uint8_t)(bs / n); }
        else {
            vw_prgb[i][0] = (uint8_t)(((bx->lo[0] + bx->hi[0]) << 2) | 4);
            vw_prgb[i][1] = (uint8_t)(((bx->lo[1] + bx->hi[1]) << 2) | 4);
            vw_prgb[i][2] = (uint8_t)(((bx->lo[2] + bx->hi[2]) << 2) | 4);
        }
    }
}

static int16_t vw_er[2][(VW_W + 2) * 3];       /* the error of the row that is made and of the next one, per colour, in 1/16 */

/* one row: rgb[n] -> entries dst[n] (1 ..); cur = 0 / 1 tells which of the two error rows is this row */
static void vw_dither_row(const uint8_t *rgb, uint8_t *dst, int n, int cur)
{
    int16_t *e0 = vw_er[cur] + 3, *e1 = vw_er[1 - cur] + 3;
    int x, k;
    for (x = 0; x < n; x++) {
        int c[3], idx;
        const uint8_t *p;
        for (k = 0; k < 3; k++) c[k] = vw_clamp(rgb[x * 3 + k] + ((e0[x * 3 + k] + 8) >> 4), 0, 255);
        idx = vw_inv[HIDX(c[0] >> 3, c[1] >> 3, c[2] >> 3)];
        dst[x] = (uint8_t)idx;
        p = vw_prgb[idx - 1];
        for (k = 0; k < 3; k++) {
            int er = c[k] - p[k];
            e0[(x + 1) * 3 + k] += (int16_t)(er * 7);
            e1[(x - 1) * 3 + k] += (int16_t)(er * 3);
            e1[x * 3 + k]       += (int16_t)(er * 5);
            e1[(x + 1) * 3 + k] += (int16_t)er;
        }
    }
    memset(vw_er[cur], 0, sizeof vw_er[cur]);
}

/* the whole layout into the 8-bit frame (the rectangle of the picture only; vw_frame_blank made the rest) */
static void vw_render_pal(const vw_tile *t, const vw_lay *L, uint8_t *frame)
{
    static uint8_t rgb[VW_W * 3];
    int Y;
    vw_hist_make(t, L);
    vw_palette_make();
    memset(vw_er, 0, sizeof vw_er);
    for (Y = 0; Y < L->dh; Y++) {
        vw_sample_row(t, L, Y, rgb);
        vw_dither_row(rgb, frame + (size_t)(L->dy + Y) * VW_W + (size_t)L->dx, L->dw, Y & 1);
    }
}

/* ------------------------------------------------------------------ the frame: strips, picture, captions */
/* a 720 x 480 picture of palette entries that goes to the BMP layer in one piece: paper colour everywhere, the picture is rendered into it */
static void vw_frame_blank(uint8_t *frame)
{
    memset(frame, ui_ramp[PC_BG][3], (size_t)VW_W * VW_H);
}

/* a picture that leaves room for both strips (a 3:2 picture of the whole view: 26 rows above and 27 below) gets them; one that fills the
 * screen gets the small badge instead */
static int vw_has_bars(const vw_lay *L) { return L->dy >= VW_BAR_T && VW_H - L->dy - L->dh >= VW_BAR_B; }

static void vw_draw_bar_t(const char *name, int i, int n, int pct)
{
    char num[32], zoom[16];
    const int y = 19;                                  /* baseline in a strip 26 rows high: 17 above and 5 below fit */
    snprintf(num, sizeof num, "%d / %d", i + 1, n);
    snprintf(zoom, sizeof zoom, "%d%%", pct);
    sf_line(&sf_xxs, M_L, M_L + 260, y, 17, 5, name, PC_TEXT, SF_LEFT);
    sf_line(&sf_xxs, M_L + 260, M_R - 260, y, 17, 5, zoom, PC_ACCENT, SF_CENTER);
    sf_line(&sf_xxs, M_R - 260, M_R3, y, 17, 5, num, PC_TEXT, SF_RIGHT);
}

/* the strip below: the keys, or a message (loading, error, the name of the display mode) */
static void vw_draw_bar_b(const vw_lay *L, const char *msg)
{
    int y = L->dy + L->dh + 19;
    if (msg) sf_line(&sf_xxs, M_L, M_R, y, 17, 5, msg, PC_ACCENT, SF_CENTER);
    else     sf_line(&sf_xxs, M_L, M_R3, y, 17, 5, TR(S_VW_HINT), PC_SOFT, SF_JUST);
}

/* a small box in the lower left corner of a picture that fills the screen: name, number, zoom (it may carry a message instead) */
#define VW_BADGE_X  16
#define VW_BADGE_W  300
#define VW_BADGE_Y  448
#define VW_BADGE_H  26
static void vw_draw_badge(const char *name, int i, int n, int pct, const char *msg)
{
    char tmp[64];
    if (msg) snprintf(tmp, sizeof tmp, "%s", msg);
    else     snprintf(tmp, sizeof tmp, "%s   %d%%   %d / %d", name, pct, i + 1, n);
    sf_line(&sf_xxs, VW_BADGE_X, VW_BADGE_X + VW_BADGE_W, VW_BADGE_Y + 19, 17, 5, tmp, PC_TEXT, SF_LEFT);
}

/* the picture could not be shown: a note in the middle of the screen (inside the strips when there are any) */
static void vw_draw_note(const char *msg)
{
    sf_line(&sf_sml, M_L, M_R3, VW_H / 2 + 8, 25, 7, msg, PC_ACCENT, SF_CENTER);
}

/* the whole frame in RAM: paper, the picture (its palette is made here: vw_prgb / vw_nbx), strips or badge */
static void vw_compose(uint8_t *frame, const vw_tile *t, const vw_pic *p, const vw_lay *L, int lev, const char *name, int idx, int n)
{
    int pct = vw_percent(p, L, lev), spt = ui_pitch;
    unsigned char *sfb = ui_fb;
    ui_fb = frame; ui_pitch = VW_W;
    vw_frame_blank(frame);
    vw_render_pal(t, L, frame);
    if (vw_has_bars(L)) { vw_draw_bar_t(name, idx, n, pct); vw_draw_bar_b(L, 0); }
    else vw_draw_badge(name, idx, n, pct, 0);
    ui_fb = sfb; ui_pitch = spt;
}

/* a frame with only a note in the middle (nothing to show yet, no pictures, a picture that can not be read) */
static void vw_compose_text(uint8_t *frame, const char *msg)
{
    int spt = ui_pitch;
    unsigned char *sfb = ui_fb;
    ui_fb = frame; ui_pitch = VW_W;
    vw_frame_blank(frame);
    vw_draw_note(msg);
    ui_fb = sfb; ui_pitch = spt;
}

#endif
