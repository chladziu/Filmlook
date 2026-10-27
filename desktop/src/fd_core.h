/* fd_core.h - FILM LOOK DESKTOP: the glue between this program and the film engine of the module (modules/filmlook).
 *
 * The engine is used exactly as the camera module uses it: develop.h (fl_develop) reads the JPEG of the camera (or the
 * full size preview inside a CR2), grades it with a film (films.h), and writes a new JPEG. This file adds only what a
 * computer program needs on top:
 *   - the whole photo is in memory (a file read in one go, the result is collected in a buffer),
 *   - the three output sizes of the 5D Mark II (5616x3744, 3861x2574, 2784x1856),
 *   - readable error texts (Polish), and a list of the films for the screen.
 * Nothing under modules/filmlook is copied or changed: later improvements of the engine come in with a rebuild.
 * The only place that calls the engine is fd_run() below; if its interface changes, this is the place to adjust.
 *
 * Own code of the film look project. The engine files that get linked in are Soup Engine (GPL v2): see LICENSE. */
#ifndef FD_CORE_H
#define FD_CORE_H

#ifdef FD_WASM                                   /* the browser build: no C library at all (see fd_wasm.c) */
void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
void *memmove(void *d, const void *s, size_t n);
#else
#  ifndef SE_HOSTED
#    define SE_HOSTED 1
#  endif
#  include <stdlib.h>
#  include <stdio.h>
#endif
#include <stdint.h>
#include <stddef.h>

#include "../../modules/filmlook/develop.h"
#include "fd_resize.h"
#include "fd_zoom.h"
#include "fd_rot.h"
#include "fd_containers.h"
#include "fd_raw.h"
#include "fd_cr3.h"

#define FD_VERSION "0.1"

/* ---------------------------------------------------------------- sizes */
#define FD_SIZE_FULL   0        /* 5616 x 3744 (L): the picture as it is */
#define FD_SIZE_M      1        /* 3861 x 2574 (M) */
#define FD_SIZE_S      2        /* 2784 x 1856 (S) */
#define FD_SIZE_COUNT  3
#define FD_SIZE_ENGINE 100      /* + 1, 2 or 4: the decoder's own 1/1, 1/2 or 1/4 size (preview on the screen, tests) */

static const struct { int w, h; } fd_sizes[FD_SIZE_COUNT] = { { 5616, 3744 }, { 3861, 2574 }, { 2784, 1856 } };

/* the size a picture of sw x sh comes out at: the long side of the chosen size, the short side in proportion.
 * A picture is never enlarged: when it is not bigger than the chosen size it stays as it is. */
static void fd_target(int sw, int sh, int size, int *ow, int *oh)
{
    int ls = sw > sh ? sw : sh, ss = sw > sh ? sh : sw, lt, st;
    *ow = sw; *oh = sh;
    if (size <= FD_SIZE_FULL || size >= FD_SIZE_COUNT) return;
    lt = fd_sizes[size].w;
    if (lt >= ls) return;
    st = (int)(((int64_t)ss * lt + ls / 2) / ls);
    if (st < 1) st = 1;
    if (sw >= sh) { *ow = lt; *oh = st; } else { *ow = st; *oh = lt; }
}

/* ---------------------------------------------------------------- films */
static int fd_film_count(void) { return FILM_COUNT; }
/* which: 0 name on the screen, 1 full name (brand + name), 2 maker, 3 type (Kolor / Czarno-biały / Slajd),
 *        4 kind (one line), 5 description; the texts are Polish */
static const char *fd_film_text(int i, int which)
{
    const film_t *f;
    if (i < 0 || i >= FILM_COUNT) return "";
    f = &films[i];
    switch (which) {
    case 0: return f->name;
    case 1: return f->full;
    case 2: return groups[f->group].brand;
    case 3: return types[groups[f->group].type].name;
    case 4: return f->kind[0];
    case 5: return f->desc[0];
    default: return "";
    }
}
/* which: 0 type number, 1 ISO (0 = none), 2 black and white (1) or colour (0) */
static int fd_film_num(int i, int which)
{
    if (i < 0 || i >= FILM_COUNT) return 0;
    switch (which) {
    case 0: return groups[films[i].group].type;
    case 1: return films[i].iso;
    case 2: return films[i].mono ? 1 : 0;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- errors */
#define FD_E_FILM     -100
#define FD_E_SIZE     -101
#define FD_E_BIG      -102
static const char *fd_error_text(int rc)
{
    switch (rc) {
    case 0:                return "gotowe";
    case SE_E_BADFILE:     return "to nie jest zdjęcie JPG, CR2, CR3 ani DNG z aparatu";
    case SE_E_UNSUPPORTED: return "nieobsługiwany plik: progresywny JPG albo RAW (CR2, CR3, DNG) bez dużego podglądu JPG w środku (DNG zapisz z „pełnowymiarowym podglądem JPEG”)";
    case SE_E_TRUNCATED:   return "plik jest ucięty";
    case SE_E_NOMEM:       return "za mało pamięci na to zdjęcie";
    case SE_E_CORRUPT:     return "uszkodzone dane w pliku";
    case SE_E_IO:          return "błąd odczytu lub zapisu";
    case FD_E_FILM:        return "nie ma takiego filmu";
    case FD_E_SIZE:        return "nie udało się zmienić rozmiaru";
    case FD_E_BIG:         return "za duży rozmiar zdjęcia (najwyżej 16384 pikseli w dłuższym boku i 160 Mpix)";
    default:               return "nieznany błąd";
    }
}

/* ---------------------------------------------------------------- a job */
typedef struct {
    int film;               /* index into films[] */
    int size;               /* FD_SIZE_... */
    int quality;            /* JPEG quality of the result, 1..100 */
    look_t look;            /* Efekt, Ziarno, Kontrast, Matowość, Ciepło, Cienie, Światła, Nasycenie, Winieta (the panel's sliders) */
} fd_job;

static void fd_job_defaults(fd_job *j)          /* the defaults of every film in the panel: Efekt 100 %, Ziarno 100 %, the rest 0 */
{
    memset(j, 0, sizeof *j);
    j->size = FD_SIZE_FULL; j->quality = 98;
    j->look.strength = 100; j->look.grain = 100;
    j->look.filter_amt = 100;                   /* a colour filter, once chosen, works at full strength */
    j->look.gcolor = 100;                       /* the grain of a colour film is coloured (a black and white film ignores it) */
    j->look.rawref = 1;                         /* RAW: the tone of the camera's own JPEG is the starting point */
    j->look.hq = 1;                             /* the best quality: JPEG 4:4:4 and exact colours (0 = the camera's way: 4:2:0 and a colour table) */
}

/* ---------------------------------------------------------------- the sliders of the desktop program
 * Every number of look_t that the camera does not have, by name: the page asks for the list (fd_param_name) and sends the
 * values by index, the command line takes them as --name value, the tests use both. Adding a slider = one line here. */
#define FDP(n, field) { n, (int)offsetof(look_t, field) }
static const struct { const char *name; int off; } fd_params[] = {
    FDP("expo", expo), FDP("hi_t", hi_t), FDP("sh_t", sh_t), FDP("wh_t", wh_t), FDP("bl_t", bl_t),
    FDP("gsize", gsize), FDP("grough", grough), FDP("gcolor", gcolor), FDP("usejpg", usejpg), FDP("rawref", rawref), FDP("hq", hq),
    FDP("crop_x0", crop_x0), FDP("crop_y0", crop_y0), FDP("crop_x1", crop_x1), FDP("crop_y1", crop_y1),
    FDP("crop_ar_w", crop_ar_w), FDP("crop_ar_h", crop_ar_h), FDP("crop_ar_auto", crop_ar_auto),
    FDP("out_long", out_long), FDP("out_w", out_w), FDP("out_h", out_h),
    FDP("crop_rot", crop_rot), FDP("turn", turn),
    FDP("wb_temp", wb_temp), FDP("wb_tint", wb_tint),
    FDP("skin_hue", skin_hue), FDP("skin_sat", skin_sat), FDP("skin_lum", skin_lum),
    FDP("ton_mode", ton_mode),
    FDP("cg_sh_h", cg[0]), FDP("cg_sh_s", cg[1]), FDP("cg_mid_h", cg[2]), FDP("cg_mid_s", cg[3]),
    FDP("cg_hi_h", cg[4]), FDP("cg_hi_s", cg[5]), FDP("cg_gl_h", cg[6]), FDP("cg_gl_s", cg[7]),
    FDP("filter", filter), FDP("filter_amt", filter_amt),
    FDP("hue_red", hsl[0]), FDP("hue_orange", hsl[1]), FDP("hue_yellow", hsl[2]), FDP("hue_green", hsl[3]),
    FDP("hue_aqua", hsl[4]), FDP("hue_blue", hsl[5]), FDP("hue_purple", hsl[6]), FDP("hue_magenta", hsl[7]),
    FDP("sat_red", hsl[8]), FDP("sat_orange", hsl[9]), FDP("sat_yellow", hsl[10]), FDP("sat_green", hsl[11]),
    FDP("sat_aqua", hsl[12]), FDP("sat_blue", hsl[13]), FDP("sat_purple", hsl[14]), FDP("sat_magenta", hsl[15]),
    FDP("lum_red", hsl[16]), FDP("lum_orange", hsl[17]), FDP("lum_yellow", hsl[18]), FDP("lum_green", hsl[19]),
    FDP("lum_aqua", hsl[20]), FDP("lum_blue", hsl[21]), FDP("lum_purple", hsl[22]), FDP("lum_magenta", hsl[23]),
    /* the curves the user draws: crvK_n points of channel K (0 master, 1 red, 2 green, 3 blue), crvK_xJ / crvK_yJ the points (0..255) */
    FDP("crv0_n", uc_n[0]), FDP("crv0_x0", uc_x[0][0]), FDP("crv0_y0", uc_y[0][0]), FDP("crv0_x1", uc_x[0][1]), FDP("crv0_y1", uc_y[0][1]), FDP("crv0_x2", uc_x[0][2]), FDP("crv0_y2", uc_y[0][2]), FDP("crv0_x3", uc_x[0][3]), FDP("crv0_y3", uc_y[0][3]), FDP("crv0_x4", uc_x[0][4]), FDP("crv0_y4", uc_y[0][4]), FDP("crv0_x5", uc_x[0][5]), FDP("crv0_y5", uc_y[0][5]), FDP("crv0_x6", uc_x[0][6]), FDP("crv0_y6", uc_y[0][6]), FDP("crv0_x7", uc_x[0][7]), FDP("crv0_y7", uc_y[0][7]), FDP("crv0_x8", uc_x[0][8]), FDP("crv0_y8", uc_y[0][8]), FDP("crv0_x9", uc_x[0][9]), FDP("crv0_y9", uc_y[0][9]), FDP("crv0_x10", uc_x[0][10]), FDP("crv0_y10", uc_y[0][10]), FDP("crv0_x11", uc_x[0][11]), FDP("crv0_y11", uc_y[0][11]),
    FDP("crv1_n", uc_n[1]), FDP("crv1_x0", uc_x[1][0]), FDP("crv1_y0", uc_y[1][0]), FDP("crv1_x1", uc_x[1][1]), FDP("crv1_y1", uc_y[1][1]), FDP("crv1_x2", uc_x[1][2]), FDP("crv1_y2", uc_y[1][2]), FDP("crv1_x3", uc_x[1][3]), FDP("crv1_y3", uc_y[1][3]), FDP("crv1_x4", uc_x[1][4]), FDP("crv1_y4", uc_y[1][4]), FDP("crv1_x5", uc_x[1][5]), FDP("crv1_y5", uc_y[1][5]), FDP("crv1_x6", uc_x[1][6]), FDP("crv1_y6", uc_y[1][6]), FDP("crv1_x7", uc_x[1][7]), FDP("crv1_y7", uc_y[1][7]), FDP("crv1_x8", uc_x[1][8]), FDP("crv1_y8", uc_y[1][8]), FDP("crv1_x9", uc_x[1][9]), FDP("crv1_y9", uc_y[1][9]), FDP("crv1_x10", uc_x[1][10]), FDP("crv1_y10", uc_y[1][10]), FDP("crv1_x11", uc_x[1][11]), FDP("crv1_y11", uc_y[1][11]),
    FDP("crv2_n", uc_n[2]), FDP("crv2_x0", uc_x[2][0]), FDP("crv2_y0", uc_y[2][0]), FDP("crv2_x1", uc_x[2][1]), FDP("crv2_y1", uc_y[2][1]), FDP("crv2_x2", uc_x[2][2]), FDP("crv2_y2", uc_y[2][2]), FDP("crv2_x3", uc_x[2][3]), FDP("crv2_y3", uc_y[2][3]), FDP("crv2_x4", uc_x[2][4]), FDP("crv2_y4", uc_y[2][4]), FDP("crv2_x5", uc_x[2][5]), FDP("crv2_y5", uc_y[2][5]), FDP("crv2_x6", uc_x[2][6]), FDP("crv2_y6", uc_y[2][6]), FDP("crv2_x7", uc_x[2][7]), FDP("crv2_y7", uc_y[2][7]), FDP("crv2_x8", uc_x[2][8]), FDP("crv2_y8", uc_y[2][8]), FDP("crv2_x9", uc_x[2][9]), FDP("crv2_y9", uc_y[2][9]), FDP("crv2_x10", uc_x[2][10]), FDP("crv2_y10", uc_y[2][10]), FDP("crv2_x11", uc_x[2][11]), FDP("crv2_y11", uc_y[2][11]),
    FDP("crv3_n", uc_n[3]), FDP("crv3_x0", uc_x[3][0]), FDP("crv3_y0", uc_y[3][0]), FDP("crv3_x1", uc_x[3][1]), FDP("crv3_y1", uc_y[3][1]), FDP("crv3_x2", uc_x[3][2]), FDP("crv3_y2", uc_y[3][2]), FDP("crv3_x3", uc_x[3][3]), FDP("crv3_y3", uc_y[3][3]), FDP("crv3_x4", uc_x[3][4]), FDP("crv3_y4", uc_y[3][4]), FDP("crv3_x5", uc_x[3][5]), FDP("crv3_y5", uc_y[3][5]), FDP("crv3_x6", uc_x[3][6]), FDP("crv3_y6", uc_y[3][6]), FDP("crv3_x7", uc_x[3][7]), FDP("crv3_y7", uc_y[3][7]), FDP("crv3_x8", uc_x[3][8]), FDP("crv3_y8", uc_y[3][8]), FDP("crv3_x9", uc_x[3][9]), FDP("crv3_y9", uc_y[3][9]), FDP("crv3_x10", uc_x[3][10]), FDP("crv3_y10", uc_y[3][10]), FDP("crv3_x11", uc_x[3][11]), FDP("crv3_y11", uc_y[3][11])
};
#define FD_PARAM_COUNT ((int)(sizeof fd_params / sizeof fd_params[0]))

static const char *fd_param_name(int k) { return (k >= 0 && k < FD_PARAM_COUNT) ? fd_params[k].name : ""; }
static void fd_job_set(fd_job *j, int k, int v)
{
    if (k >= 0 && k < FD_PARAM_COUNT) *(int *)((char *)&j->look + fd_params[k].off) = v;
}
/* by name; returns 1 when the name exists */
static int fd_job_set_name(fd_job *j, const char *name, int v)
{
    int k;
    for (k = 0; k < FD_PARAM_COUNT; k++) {
        const char *a = fd_params[k].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) { fd_job_set(j, k, v); return 1; }
    }
    return 0;
}

typedef struct { uint8_t *data; size_t len, cap; int fail; } fd_buf;     /* the JPEG that comes out */

typedef struct {
    int rc;                 /* 0 = ok, otherwise an se_err or FD_E_... */
    int src_w, src_h, out_w, out_h;   /* src: the part of the photo that was used (the crop), full size pixels, as stored; out: the result, as stored */
    int pic_w, pic_h;       /* the whole photo as it is shown (the camera's orientation applied), full size pixels */
    int swap;               /* 1 = the camera's orientation turns the picture by 90 degrees (out_w x out_h are then the other way round when shown) */
    size_t mem_peak;
    int raw;                /* 1 = the picture came from the sensor data (RAW), 0 = from the JPEG of the file */
    int raw_err;            /* when the RAW could not be used and the JPEG was taken instead: why (an se_err), else 0 */
} fd_result;

typedef struct { const uint8_t *p; size_t n; } fd_src;
static int fd_rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    const fd_src *s = (const fd_src *)ctx;
    if ((size_t)off + n > s->n) return 1;
    memcpy(dst, s->p + off, n);
    return 0;
}
static int fd_wr(void *ctx, const uint8_t *d, size_t n)
{
    fd_buf *b = (fd_buf *)ctx;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : ((size_t)1 << 20);
        uint8_t *p;
        while (nc < b->len + n) nc += nc / 2;
        p = (uint8_t *)realloc(b->data, nc);
        if (!p) { b->fail = 1; return 1; }
        b->data = p; b->cap = nc;
    }
    memcpy(b->data + b->len, d, n);
    b->len += n;
    return 0;
}

/* ------------------------------------------------- the geometry: the crop and the size of the result
 * One place that decides, for a picture of sw x sh (as stored in the file), which part of it is used and how big the result
 * is. The crop and the size come from look_t (crop_*, out_*, see xmp.h); the old size codes (M, S, the engine's own 1/2 and
 * 1/4) still work when no crop and no size are given. All numbers are for the picture as it is stored (the camera's
 * orientation is only in the EXIF), except the ones that come from the page, which are for the picture as it is shown. */
#define FD_MAX_SIDE 16384                 /* the longest side of a result (the JPEG format itself allows 65535) */
#define FD_MAX_PIX  160000000LL           /* and the most pixels (160 Mpix: a JPEG of a few hundred MB) */

typedef struct {
    int div;                 /* 1, 2 or 4: the decoder / the sensor binning step the picture is taken at */
    int bw, bh;              /* the whole picture at that step */
    int cx, cy, cw, ch;      /* the part of it that is used */
    int ow, oh;              /* the result */
    int src_w, src_h;        /* the used part in full size pixels (what the page is told) */
    int route;               /* 0 = as it is, 1 = smaller (area average), 2 = bigger (bicubic) */
    int orient;              /* the orientation of the result (the file's own, turned by look_t.turn) */
    int rot;                 /* the straightening angle for the picture as it is stored, thousandths of a degree (0 = none) */
} fd_geo;

static int fd_has_geo(const look_t *L)
{
    return (L->crop_x1 > L->crop_x0 && L->crop_y1 > L->crop_y0) || (L->crop_ar_w > 0 && L->crop_ar_h > 0) ||
           L->out_long > 0 || L->out_w > 0 || L->out_h > 0 || L->crop_rot != 0 || (L->turn & 3) != 0;
}

/* The orientation of a photo (EXIF 1..8) turned by `turn` quarter turns clockwise as it is shown. Every orientation is a 2 x 2
 * matrix (stored x, y -> shown x, y); a quarter turn multiplies it by [0 -1; 1 0]. */
static int fd_orient_turn(int o, int turn)
{
    static const signed char M[9][4] = { { 1, 0, 0, 1 }, { 1, 0, 0, 1 }, { -1, 0, 0, 1 }, { -1, 0, 0, -1 }, { 1, 0, 0, -1 },
                                         { 0, 1, 1, 0 }, { 0, -1, 1, 0 }, { 0, -1, -1, 0 }, { 0, 1, -1, 0 } };
    int a, b, c, d, k;
    if (o < 1 || o > 8) o = 1;
    a = M[o][0]; b = M[o][1]; c = M[o][2]; d = M[o][3];
    for (k = turn & 3; k > 0; k--) { int na = -c, nb = -d, nc = a, nd = b; a = na; b = nb; c = nc; d = nd; }
    for (k = 1; k <= 8; k++) if (M[k][0] == a && M[k][1] == b && M[k][2] == c && M[k][3] == d) return k;
    return 1;
}

static int fd_gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
static int fd_frac(int f, int n)         /* millionths of n, rounded */
{
    if (f < 0) f = 0;
    if (f > 1000000) f = 1000000;
    return (int)(((int64_t)f * n + 500000) / 1000000);
}
static int fd_round_div(int64_t a, int64_t b) { return (int)((a + b / 2) / b); }

static int fd_pic_w = 0, fd_pic_h = 0, fd_pic_swap = 0;     /* the whole photo as it is shown (and whether it is turned): set by every develop (see fd_run) */

/* orient: the EXIF orientation 1..8 (0 = 1). 0 = ok, otherwise an error code */
static int fd_geo_make(int sw, int sh, int orient, int size, const look_t *L, fd_geo *g)
{
    int div = 1, swap, rot = L->crop_rot;
    memset(g, 0, sizeof *g);
    if (sw < 1 || sh < 1) return FD_E_SIZE;
    orient = fd_orient_turn(orient, L->turn);                       /* the turn of the page is a turn of the orientation tag */
    swap = (orient >= 5 && orient <= 8);
    g->orient = orient;
    if (rot > 90000) rot = 90000;
    if (rot < -90000) rot = -90000;
    fd_pic_w = swap ? sh : sw; fd_pic_h = swap ? sw : sh; fd_pic_swap = swap;
    if (size > FD_SIZE_ENGINE) { div = size - FD_SIZE_ENGINE; if (div != 2 && div != 4) div = 1; }
    g->div = div;
    g->bw = (sw + div - 1) / div; g->bh = (sh + div - 1) / div;
    if (!fd_has_geo(L)) {                                         /* no crop, no size: the old way (the codes of the 5D) */
        g->cw = g->bw; g->ch = g->bh;
        if (div == 1) fd_target(sw, sh, size, &g->ow, &g->oh); else { g->ow = g->bw; g->oh = g->bh; }
        g->src_w = sw; g->src_h = sh;
    } else {
        int dW = swap ? g->bh : g->bw, dH = swap ? g->bw : g->bh;          /* the picture as it is shown */
        int x0 = 0, y0 = 0, x1 = dW, y1 = dH, sx0, sy0, sx1, sy1, cwd, chd, owd, ohd, W = g->bw, H = g->bh;
        if (L->crop_x1 > L->crop_x0 && L->crop_y1 > L->crop_y0) {
            x0 = fd_frac(L->crop_x0, dW); x1 = fd_frac(L->crop_x1, dW);
            y0 = fd_frac(L->crop_y0, dH); y1 = fd_frac(L->crop_y1, dH);
            if (x0 > dW - 1) x0 = dW - 1;
            if (y0 > dH - 1) y0 = dH - 1;
            if (x1 < x0 + 1) x1 = x0 + 1;
            if (y1 < y0 + 1) y1 = y0 + 1;
        }
        if (rot) {                                                      /* the crop has to lie inside the turned picture */
            int64_t sn, cs;
            int s16, w = x1 - x0, h = y1 - y0, mx2 = x0 + x1 - dW, my2 = y0 + y1 - dH;
            fd_sincos(rot, &sn, &cs);
            s16 = fd_rot_fit(sn, cs, dW, dH, mx2, my2, w, h, (L->crop_x1 > L->crop_x0 && L->crop_y1 > L->crop_y0) ? 2 : 0);   /* (a crop that is two pixels outside is let through: the edge is repeated) */
            if (s16 < 65536) {
                if (s16 > 0) s16 = fd_rot_fit(sn, cs, dW, dH, mx2, my2, w, h, 0);
                if (s16 == 0) { x0 = 0; y0 = 0; x1 = dW; y1 = dH; w = dW; h = dH; mx2 = 0; my2 = 0; s16 = fd_rot_fit(sn, cs, dW, dH, 0, 0, w, h, 0); }
                if (s16 < 65536) {
                    int nw = (int)(((int64_t)w * s16) >> 16), nh = (int)(((int64_t)h * s16) >> 16);
                    if (nw < 1) nw = 1;
                    if (nh < 1) nh = 1;
                    x0 = (x0 + x1 - nw) / 2; x1 = x0 + nw;
                    y0 = (y0 + y1 - nh) / 2; y1 = y0 + nh;
                }
            }
        }
        if (L->crop_ar_w > 0 && L->crop_ar_h > 0) {                     /* the biggest crop of that shape in the middle of the rectangle */
            int aw = L->crop_ar_w > 100000 ? 100000 : L->crop_ar_w, ah = L->crop_ar_h > 100000 ? 100000 : L->crop_ar_h;
            int bwid = x1 - x0, bhei = y1 - y0, gg, k, w, h;
            if (L->crop_ar_auto && aw != ah && bwid != bhei && ((bwid < bhei) != (aw < ah))) { int t = aw; aw = ah; ah = t; }
            gg = fd_gcd(aw, ah); aw /= gg; ah /= gg;
            k = bwid / aw; if (bhei / ah < k) k = bhei / ah;
            if (k >= 1) { w = k * aw; h = k * ah; }                     /* exactly that ratio */
            else if ((int64_t)bwid * ah >= (int64_t)bhei * aw) { h = bhei; w = (int)(((int64_t)h * aw) / ah); }
            else { w = bwid; h = (int)(((int64_t)w * ah) / aw); }
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            x0 += (bwid - w) / 2; y0 += (bhei - h) / 2; x1 = x0 + w; y1 = y0 + h;
        }
        switch (orient) {                                               /* from the picture as shown to the picture as stored */
        case 2:  sx0 = W - x1; sx1 = W - x0; sy0 = y0;     sy1 = y1;     break;
        case 3:  sx0 = W - x1; sx1 = W - x0; sy0 = H - y1; sy1 = H - y0; break;
        case 4:  sx0 = x0;     sx1 = x1;     sy0 = H - y1; sy1 = H - y0; break;
        case 5:  sx0 = y0;     sx1 = y1;     sy0 = x0;     sy1 = x1;     break;
        case 6:  sx0 = y0;     sx1 = y1;     sy0 = H - x1; sy1 = H - x0; break;
        case 7:  sx0 = W - y1; sx1 = W - y0; sy0 = H - x1; sy1 = H - x0; break;
        case 8:  sx0 = W - y1; sx1 = W - y0; sy0 = x0;     sy1 = x1;     break;
        default: sx0 = x0;     sx1 = x1;     sy0 = y0;     sy1 = y1;     break;
        }
        g->cx = sx0; g->cy = sy0; g->cw = sx1 - sx0; g->ch = sy1 - sy0;
        if (g->cx < 0 || g->cy < 0 || g->cw < 1 || g->ch < 1 || g->cx + g->cw > W || g->cy + g->ch > H) return FD_E_SIZE;
        cwd = swap ? g->ch : g->cw; chd = swap ? g->cw : g->ch;
        if (L->out_long > 0)      { if (cwd >= chd) { owd = L->out_long; ohd = fd_round_div((int64_t)chd * owd, cwd); }
                                    else            { ohd = L->out_long; owd = fd_round_div((int64_t)cwd * ohd, chd); } }
        else if (L->out_w > 0)    { owd = L->out_w; ohd = fd_round_div((int64_t)chd * owd, cwd); }
        else if (L->out_h > 0)    { ohd = L->out_h; owd = fd_round_div((int64_t)cwd * ohd, chd); }
        else                      { owd = cwd; ohd = chd; }
        if (owd < 1) owd = 1;
        if (ohd < 1) ohd = 1;
        if (owd > FD_MAX_SIDE || ohd > FD_MAX_SIDE || (int64_t)owd * ohd > FD_MAX_PIX) return FD_E_BIG;
        g->ow = swap ? ohd : owd; g->oh = swap ? owd : ohd;
        g->src_w = g->cw * div; g->src_h = g->ch * div;
        if (g->src_w > sw) g->src_w = sw;
        if (g->src_h > sh) g->src_h = sh;
        /* the angle of the picture as it is stored: a mirrored orientation turns the other way round */
        g->rot = (orient == 2 || orient == 4 || orient == 5 || orient == 7) ? -rot : rot;
    }
    if (g->ow == g->cw && g->oh == g->ch) g->route = 0;
    else if (g->ow <= g->cw && g->oh <= g->ch) g->route = 1;
    else if (g->ow >= g->cw && g->oh >= g->ch) g->route = 2;
    else return FD_E_SIZE;
    return 0;
}

/* the widest row the engine will see for this job: the picture itself or the result, whichever is wider (memory is sized by it) */
static int fd_geo_width(int sw, int sh, const look_t *L)
{
    int m = sw > sh ? sw : sh;
    if (m < 6000) m = 6000;
    if (L->out_long > m) m = L->out_long;
    if (L->out_w > m) m = L->out_w;
    if (L->out_h > m) m = L->out_h;
    return m > FD_MAX_SIDE ? FD_MAX_SIDE : m;
}

/* ------------------------------------------------- the JPEG route with a crop and a size
 * decoder -> the crop (only the wanted rows and columns go on) -> smaller (area average, fd_resize.h) or bigger (bicubic, fd_zoom.h)
 * -> film look -> encoder. The film look runs on the finished picture, so the grain, the halation and the vignette belong to
 * the result (a vignette is round in the cropped picture, not in the original). This is fl_develop() of develop.h with those stages
 * added; for the sizes M and S of the 5D it makes the very same pictures as before. */
typedef struct {
    int y, x0, y0, cw, ch, done;
    int (*sink)(void *ctx, const uint8_t *rgb, int nrows, int width);
    void *sctx;
} fd_cropf;

static int fd_crop_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fd_cropf *c = (fd_cropf *)ctx;
    int k;
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        int y = c->y++;
        if (y < c->y0) continue;
        if (y >= c->y0 + c->ch) { c->done = 1; return -1; }
        if (c->sink(c->sctx, rgb + (size_t)c->x0 * 3, 1, c->cw) != 0) return -1;
        if (y == c->y0 + c->ch - 1) { c->done = 1; return -1; }      /* the rest of the picture is not needed: stop the decoder */
    }
    return 0;
}

static void fd_develop_geo(const fl_io *io, const film_t *f, const look_t *L, int size, int quality, fl_result *res)
{
    se_arena ar, lookar;
    se_look lk;
    se_jdec_info ji;
    se_jpeg_exif ex;
    fl_meta meta;
    fl_ctx c;
    fd_rs rs;
    fd_zs8 zs;
    fd_rot8 rt;
    fd_cropf cf;
    fd_geo g;
    uint8_t *hdr, *strip;
    uint32_t hlen, poff = 0, plen = 0;
    int gscale, rs_open = 0, zs_open = 0, rt_open = 0, grc;
    int (*dsink)(void *ctx, const uint8_t *rgb, int nrows, int width);
    void *dctx;
    se_err rc;

    memset(res, 0, sizeof *res);
    memset(&rs, 0, sizeof rs); memset(&zs, 0, sizeof zs); memset(&rt, 0, sizeof rt);
    se_arena_init(&ar, io->mem, io->mem_size);
    hdr = SE_NEW(&ar, uint8_t, FL_HDR_BYTES);
    if (!hdr) { res->rc = SE_E_NOMEM; return; }
    hlen = io->src_size < FL_HDR_BYTES ? io->src_size : FL_HDR_BYTES;
    if (io->read(io->rctx, 0, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = fl_find_jpeg(hdr, hlen, io->src_size, &poff, &plen, &meta);
    if (rc != SE_OK) { res->rc = rc; return; }
    if (plen > io->src_size || poff > io->src_size - plen) { res->rc = SE_E_TRUNCATED; return; }
    hlen = plen < FL_HDR_BYTES ? plen : FL_HDR_BYTES;
    if (io->read(io->rctx, poff, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = se_jpeg_dec_info(hdr, hlen, &ji);
    if (rc != SE_OK) { res->rc = rc; return; }
    grc = fd_geo_make(ji.width, ji.height, meta.orient, size, L, &g);
    if (grc) { res->rc = grc; return; }
    res->src_w = g.src_w; res->src_h = g.src_h; res->out_w = g.ow; res->out_h = g.oh; res->scale = g.div;
    gscale = (g.cw * g.div + g.ow / 2) / g.ow;       /* grain and halation are sized for a whole step 1, 2 ...: the nearest one */
    if (gscale < 1) gscale = 1;

    {
        size_t tabs = 3 * 1536 * sizeof(int16_t) + 64;
        size_t budget = SE_LOOK_BUDGET + (size_t)g.ow * 4 + 4096 + fl_look_extra(g.ow, L);
        uint8_t *mem = (uint8_t *)se_arena_alloc(&ar, tabs + budget, 8);
        if (!mem) { res->rc = SE_E_NOMEM; return; }
        se_arena_init(&lookar, mem, tabs + budget);
        if (fl_build_look(&lk, &lookar, f, L, gscale, g.oh) != 0) { res->rc = SE_E_NOMEM; return; }
        (void)se_look_fit(&lk, g.ow, budget);
        res->look_radius = lk.hal_amount > 0 ? lk.hal_radius : 0;
        rc = se_look_begin(&c.run, &lk, g.ow, &lookar);
        if (rc != SE_OK) { res->rc = rc; return; }
    }

    c.io = io; c.base = poff; c.ow = g.ow; c.err = 0; c.rows = 0; c.oh = g.oh > 0 ? g.oh : 1;
    c.enc = SE_NEW(&ar, se_jpeg, 1);
    strip = SE_NEW(&ar, uint8_t, SE_JPEG_STRIP_BYTES(g.ow));
    if (!c.enc || !strip) { res->rc = SE_E_NOMEM; return; }
    memset(&cf, 0, sizeof cf);
    cf.x0 = g.cx; cf.y0 = g.cy; cf.cw = g.cw; cf.ch = g.ch;
    if (g.route == 1) {
        if (fd_rs_begin(&rs, g.cw, g.ch, g.ow, g.oh, fl_look_rows, &c) != 0) { res->rc = FD_E_SIZE; return; }
        rs_open = 1; dsink = fd_rs_rows; dctx = &rs;
    } else if (g.route == 2) {
        if (fd_zs8_begin(&zs, g.cw, g.ch, g.ow, g.oh, fl_look_rows, &c) != 0) { res->rc = FD_E_SIZE; return; }
        zs_open = 1; dsink = fd_zs8_rows; dctx = &zs;
    } else { dsink = fl_look_rows; dctx = &c; }
    cf.sink = dsink; cf.sctx = dctx;
    if (g.rot) {                                         /* straightening: the decoder feeds the band of rows the turned crop needs */
        if (fd_rot8_begin(&rt, g.bw, g.bh, g.cx, g.cy, g.cw, g.ch, g.rot, dsink, dctx) != 0) {
            if (rs_open) fd_rs_free(&rs);
            if (zs_open) fd_zs8_free(&zs);
            res->rc = SE_E_NOMEM; return;
        }
        rt_open = 1;
        cf.x0 = 0; cf.cw = g.bw; cf.y0 = rt.r.y0; cf.ch = rt.r.y1 - rt.r.y0 + 1;
        cf.sink = fd_rot8_rows; cf.sctx = &rt;
    }

    memset(&ex, 0, sizeof ex);
    ex.make = meta.make[0] ? meta.make : "Canon";
    ex.model = meta.model[0] ? meta.model : "Canon EOS 5D Mark II";
    ex.software = f->full;
    ex.datetime = meta.dt;
    ex.exp_num = meta.en; ex.exp_den = meta.ed; ex.fn_num = meta.fn; ex.fn_den = meta.fd;
    ex.iso = meta.iso; ex.orientation = g.orient;
    rc = se_jpeg_begin(c.enc, g.ow, g.oh, quality, strip, &ex, fl_enc_sink, &c);
    if (rc != SE_OK) { if (rs_open) fd_rs_free(&rs); if (zs_open) fd_zs8_free(&zs); if (rt_open) fd_rot8_free(&rt); res->rc = rc; return; }

    rc = se_jpeg_decode_stream_scaled(hdr, hlen, plen, fl_src_read, &c, g.div, &ar, fd_crop_rows, &cf, &ji);
    if (rc == SE_E_IO && cf.done && !c.err && !rs.failed && !zs.z.failed && !rt.r.failed) rc = SE_OK;    /* the decoder was stopped on purpose */
    if (rc == SE_OK && (c.err || rs.failed || zs.z.failed || rt.r.failed)) rc = SE_E_IO;
    if (rc == SE_OK && cf.y0 + cf.ch > cf.y) rc = SE_E_TRUNCATED;                         /* the picture ended before the crop did */
    if (rc == SE_OK && rt_open && !fd_rot_done(&rt.r)) rc = SE_E_TRUNCATED;
    if (rc == SE_OK) rc = se_look_end(c.run, fl_look_emit, &c);
    if (rc == SE_OK && !c.err) rc = se_jpeg_end(c.enc);
    if (rc == SE_OK && c.err) rc = SE_E_IO;
    if (rs_open) fd_rs_free(&rs);
    if (zs_open) fd_zs8_free(&zs);
    if (rt_open) fd_rot8_free(&rt);
    res->rc = rc;
    res->mem_peak = ar.peak;
}

/* ================================================================== RAW
 * The sensor data of a photo (a CR2 of the 5D Mark II, a CR3 of the EOS R series, a DNG) is decoded once (fd_raw_load) and kept in a fd_rawdata. A job
 * then develops it row by row: demosaic -> 16 bit linear light (fd_raw.h) -> the first tone stage of the film made on that
 * light (fr_build_map) -> 8 bit rows -> the rest of the film look of the engine -> the JPEG.
 * The tone of the camera's own JPEG is the starting point of the tone (the switch "wzorzec z JPG"): fd_raw_fit() measures it
 * on the JPEG that is inside the file. Without that switch the tone is the plain sRGB curve. */
typedef struct {
    uint16_t *raw;           /* the sensor data: ri.rw x ri.rh values (x 3 for a linear DNG) */
    fr_info ri;
    int16_t ref[257];        /* the tone of the camera's JPEG as a function of the plain sRGB code, 0..4096 at every 16th step */
    int fit;                 /* 1 = ref was measured on the JPEG of this file, 0 = the generic curve of the 5D Mark II */
} fd_rawdata;

/* the page keeps the photo that is open decoded: fd_pre belongs to the source starting at fd_pre_src (see fd_wasm.c) */
static fd_rawdata *fd_pre = 0;
static const uint8_t *fd_pre_src = 0;
static size_t fd_pre_len = 0;
static int fd_pre_rc = 0;            /* what decoding it gave: SE_OK, FR_NONE (no usable sensor data) or an error */

static void fd_raw_free(fd_rawdata *rd) { free(rd->raw); rd->raw = 0; }

/* the average tone of the Canon JPEG of the 5D Mark II (measured on four photos), tenths of a code at the sRGB codes 0, 16 ... 256 */
static const int16_t fd_gen_t[17] = { 0, 135, 308, 513, 740, 980, 1223, 1468, 1669, 1803, 1933, 2060, 2184, 2297, 2387, 2475, 2550 };
static int fd_gen_ref(int b)                       /* b = 0..256 -> 0..4096 */
{
    int i = b >> 4, fr = b & 15, v;
    if (i >= 16) v = fd_gen_t[16];
    else v = fd_gen_t[i] + (((fd_gen_t[i + 1] - fd_gen_t[i]) * fr + 8) >> 4);
    return (v * 4096 + 1275) / 2550;
}

/* ---- the reference: JPEG tone against the RAW. Both are compared pixel by pixel at 1/2 size (the JPEG is decoded at half size,
 * the RAW is binned): for every sRGB code of the linear light, which codes does the JPEG have there? The middle half of the
 * values of each code gives the curve. */
typedef struct {
    fr_ctx *c;
    const uint16_t *l2s;         /* linear 16 bit -> sRGB 0..4096 */
    uint32_t *hist;              /* 257 x 256 */
    uint16_t *brow;              /* the binned RAW row that was made last */
    int brow_y;
    int dw, dh, bw, bh, div, y;  /* decoded JPEG size, binned RAW size, the factor, the JPEG row that comes next */
} fd_fit;

static int fd_fit_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fd_fit *f = (fd_fit *)ctx;
    int k;
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        int y = f->y++, by, x;
        if ((y & 1) || y >= f->dh) continue;                                   /* every second row and every second column is plenty */
        by = (int)((int64_t)y * f->bh / f->dh);
        if (by != f->brow_y) { fr_row_binned(f->c, by, f->div, f->bw, f->brow); f->brow_y = by; }
        for (x = 0; x < width && x < f->dw; x += 2) {
            int bx = (int)((int64_t)x * f->bw / f->dw), ch;
            for (ch = 0; ch < 3; ch++) {
                int b = (f->l2s[f->brow[bx * 3 + ch]] + 8) >> 4;
                f->hist[(size_t)b * 256 + rgb[x * 3 + ch]]++;
            }
        }
    }
    return 0;
}

/* the histogram -> ref[257]; 0 when there is too little to go by (then the generic curve is used) */
static int fd_fit_finish(const uint32_t *hist, int16_t *ref)
{
    int val[257], ok[257], b, k, nok = 0, f0 = -1, l0 = -1, pass;
    for (b = 0; b <= 256; b++) {
        const uint32_t *h = hist + (size_t)b * 256;
        uint32_t n = 0, lo, hi, seen = 0;
        int64_t sum = 0, cnt = 0, m16;
        for (k = 0; k < 256; k++) n += h[k];
        ok[b] = 0; val[b] = 0;
        if (n < 48) continue;
        lo = n / 4; hi = n - n / 4;
        for (k = 0; k < 256; k++) {                                            /* the middle half of the samples */
            uint32_t a = seen, e = seen + h[k], from = a > lo ? a : lo, to = e < hi ? e : hi;
            if (to > from) { sum += (int64_t)(to - from) * k; cnt += to - from; }
            seen = e;
        }
        if (cnt <= 0) continue;
        m16 = (sum * 16 + cnt / 2) / cnt;                                      /* the mean in 1/16 codes */
        val[b] = (int)((m16 * 256 + 127) / 255);                               /* ... in 0..4096 */
        ok[b] = 1; nok++;
        if (f0 < 0) f0 = b;
        l0 = b;
    }
    if (nok < 12) return 0;
    for (b = 0; b <= 256; b++) {
        if (ok[b]) continue;
        if (b < f0) val[b] = val[f0] + fd_gen_ref(b) - fd_gen_ref(f0);       /* before the first / after the last measured code: the shape of the generic curve */
        else if (b > l0) val[b] = val[l0] + fd_gen_ref(b) - fd_gen_ref(l0);
        else {
            int p = b - 1, q = b + 1;
            while (!ok[p]) p--;
            while (!ok[q]) q++;
            val[b] = val[p] + ((val[q] - val[p]) * (b - p) + (q - p) / 2) / (q - p);
        }
        ok[b] = 2;
    }
    for (pass = 0; pass < 2; pass++) {                                         /* smooth, the ends stay */
        int prev = val[0];
        for (b = 1; b < 256; b++) { int cur = val[b]; val[b] = (prev + 2 * cur + val[b + 1] + 2) >> 2; prev = cur; }
    }
    for (b = 0; b <= 256; b++) {                                               /* a tone curve never goes down */
        if (val[b] < 0) val[b] = 0;
        if (val[b] > 4096) val[b] = 4096;
        if (b > 0 && val[b] < val[b - 1]) val[b] = val[b - 1];
        ref[b] = (int16_t)val[b];
    }
    return 1;
}

/* the JPEG inside the file (the camera's picture) */
static int fd_raw_find_jpeg(const uint8_t *src, size_t n, size_t *off, size_t *len)
{
    fl_meta m;
    if (n >= 16 && src[0] == 'I' && src[1] == 'I' && src[2] == 42 && src[8] == 'C' && src[9] == 'R') {
        uint32_t po = 0, pl = 0, hl = n < FL_HDR_BYTES ? (uint32_t)n : FL_HDR_BYTES;
        if (fl_find_jpeg(src, hl, (uint32_t)n, &po, &pl, &m) != SE_OK || (size_t)po + pl > n) return 0;
        *off = po; *len = pl;
        return 1;
    }
    if (n >= 12 && src[4] == 'f' && src[5] == 't' && src[6] == 'y' && src[7] == 'p') return fd_cr3_find(src, n, off, len, &m) == SE_OK;
    return fd_dng_find(src, n, off, len, &m) == SE_OK;
}

static void fd_raw_fit(fd_rawdata *rd, const uint8_t *src, size_t n)
{
    size_t po = 0, pl = 0, am;
    se_jdec_info ji;
    fd_fit ft;
    fr_ctx fc;
    uint16_t *l2s = 0;
    uint32_t *hist = 0;
    uint16_t *brow = 0;
    uint8_t *mem = 0;
    int scale, i, ok = 0;
    se_arena ar;
    const fr_info *ri = &rd->ri;

    for (i = 0; i <= 256; i++) rd->ref[i] = (int16_t)fd_gen_ref(i);
    rd->fit = 0;
    if (!fd_raw_find_jpeg(src, n, &po, &pl)) return;
    if (se_jpeg_dec_info(src + po, pl < FL_HDR_BYTES ? pl : FL_HDR_BYTES, &ji) != SE_OK) return;
    if (ji.width < 16 || ji.height < 16 || ri->w < 16 || ri->h < 16) return;    /* a damaged header: nothing to compare with */
    scale = (ji.width * 3 > ri->w * 2) ? 2 : 1;                                 /* a full size JPEG is decoded at half size */
    ft.dw = (ji.width + scale - 1) / scale; ft.dh = (ji.height + scale - 1) / scale;
    ft.div = (ri->w + ft.dw / 2) / ft.dw;
    if (ft.div < 2 || ft.div > 16) return;
    ft.bw = (ri->w + ft.div - 1) / ft.div; ft.bh = (ri->h + ft.div - 1) / ft.div;
    /* the same picture: the sizes must agree within 6 % in both directions (a cropped or a rotated JPEG is of no use) */
    if (ft.dw * ft.div > ri->w + ri->w / 16 || ft.dw * ft.div < ri->w - ri->w / 16) return;
    if (ft.dh * ft.div > ri->h + ri->h / 16 || ft.dh * ft.div < ri->h - ri->h / 16) return;

    l2s = (uint16_t *)malloc(65536 * sizeof(uint16_t));
    hist = (uint32_t *)malloc((size_t)257 * 256 * sizeof(uint32_t));
    if (hist) memset(hist, 0, (size_t)257 * 256 * sizeof(uint32_t));
    brow = (uint16_t *)malloc((size_t)ft.bw * 3 * sizeof(uint16_t));
    am = fl_mem_needed(ji.width, scale) + 65536;
    mem = (uint8_t *)malloc(am);
    if (!l2s || !hist || !brow || !mem) goto done;
    for (i = 0; i < 65536; i++) l2s[i] = (uint16_t)lb_l2s(i);
    fr_prepare(&fc, ri, rd->raw);
    ft.c = &fc; ft.l2s = l2s; ft.hist = hist; ft.brow = brow; ft.brow_y = -1; ft.y = 0;
    se_arena_init(&ar, mem, am);
    if (se_jpeg_decode_scaled(src + po, pl, scale, &ar, fd_fit_rows, &ft, &ji) != SE_OK) goto done;
    ok = fd_fit_finish(hist, rd->ref);
    if (!ok) for (i = 0; i <= 256; i++) rd->ref[i] = (int16_t)fd_gen_ref(i);
    rd->fit = ok;
done:
    free(mem); free(brow); free(hist); free(l2s);
}

/* ---- decoding the sensor data of a file. SE_OK, FR_NONE (nothing here that the program can use: the JPEG is taken), or an error */
static int fd_raw_load(const uint8_t *src, size_t n, fd_rawdata *rd)
{
    int rc;
    size_t cnt;
    uint16_t *rowbuf;
    fr_ctx fc;
    memset(rd, 0, sizeof *rd);
    if (n < 64) return FR_NONE;
    if (src[0] == 'I' && src[1] == 'I' && src[2] == 42 && src[3] == 0 && src[8] == 'C' && src[9] == 'R') rc = fr_parse_cr2(src, n, &rd->ri);
    else if ((src[0] == 'I' && src[1] == 'I' && src[2] == 42 && src[3] == 0) || (src[0] == 'M' && src[1] == 'M' && src[2] == 0 && src[3] == 42))
        rc = fr_parse_dng(src, n, &rd->ri);
    else if (src[4] == 'f' && src[5] == 't' && src[6] == 'y' && src[7] == 'p') rc = fr_parse_cr3(src, n, &rd->ri);        /* Canon CR3 (EOS R ...) */
    else return FR_NONE;
    if (rc) return rc;
    cnt = (size_t)rd->ri.rw * (size_t)rd->ri.rh * (size_t)(rd->ri.ncomp == 3 ? 3 : 1);
    rd->raw = (uint16_t *)malloc(cnt * sizeof(uint16_t));
    rowbuf = rd->ri.kind == 3 ? 0 : (uint16_t *)malloc((rd->ri.kind == 2 ? fr_dng_rowbuf(&rd->ri) : (size_t)rd->ri.rw * 4) * sizeof(uint16_t));
    if (!rd->raw || (!rowbuf && rd->ri.kind != 3)) { free(rowbuf); fd_raw_free(rd); return SE_E_NOMEM; }
    if (rd->ri.kind == 3) rc = fr_decode_cr3(src, n, &rd->ri, rd->raw);
    else if (rd->ri.kind == 2) { memset(rd->raw, 0, cnt * sizeof(uint16_t)); rc = fr_decode_dng(src, n, &rd->ri, rd->raw, rowbuf); }
    else rc = fr_decode_cr2(src, &rd->ri, rd->raw, rowbuf);
    free(rowbuf);
    if (rc) { fd_raw_free(rd); return rc; }
    fr_prepare(&fc, &rd->ri, rd->raw);                      /* the black level, measured once */
    { int p; for (p = 0; p < 4; p++) rd->ri.black[p] = fc.black[p]; }
    fd_raw_fit(rd, src, n);
    return SE_OK;
}

/* ---- developing */
typedef struct { fl_ctx *c; const fr_map *mp; uint8_t *row8; int y; } fd_rawout;

static int fd_raw_emit(void *ctx, const uint16_t *rgb, int nrows, int width)
{
    fd_rawout *o = (fd_rawout *)ctx;
    int k;
    for (k = 0; k < nrows; k++, rgb += (size_t)width * 3) {
        fr_to8(o->mp, rgb, width, o->y++, o->row8);
        if (fl_look_rows(o->c, o->row8, 1, width) != 0) return -1;
    }
    return 0;
}

static void fd_develop_raw(const fl_io *io, const film_t *f, const look_t *L, const fd_rawdata *rd, const uint8_t *src, size_t srclen,
                           int size, int quality, fl_result *res)
{
    se_arena ar, lookar;
    se_look lk;
    se_jpeg_exif ex;
    fl_meta meta;
    fl_ctx c;
    fr_ctx fc;
    fd_rs16 rs;
    fd_zs zs;
    fd_rot rt;
    fd_geo g;
    fd_rawout out;
    fr_map *mp = 0;
    uint16_t *rowbuf = 0;
    uint8_t *row8 = 0, *strip;
    const fr_info *ri = &rd->ri;
    int sw = ri->w, sh = ri->h, div, ow, oh, gscale, y, i, v, grc, full_open = 0, rs_open = 0, zs_open = 0, rt_open = 0;
    se_err rc = SE_OK;

    memset(res, 0, sizeof *res);
    memset(&rs, 0, sizeof rs); memset(&zs, 0, sizeof zs); memset(&rt, 0, sizeof rt);
    memset(&meta, 0, sizeof meta);
    fl_tiff_meta(src, (uint32_t)srclen, &meta);
    if (!meta.orient) meta.orient = ri->orient;
    grc = fd_geo_make(sw, sh, meta.orient, size, L, &g);
    if (grc) { res->rc = grc; return; }
    div = g.div; ow = g.ow; oh = g.oh;
    res->src_w = g.src_w; res->src_h = g.src_h; res->out_w = ow; res->out_h = oh; res->scale = div;
    gscale = (g.cw * div + ow / 2) / ow;                      /* grain and halation are sized for a whole step 1, 2 ...: the nearest one */
    if (gscale < 1) gscale = 1;

    se_arena_init(&ar, io->mem, io->mem_size);
    mp = (fr_map *)malloc(sizeof *mp);
    rowbuf = (uint16_t *)malloc((size_t)g.bw * 3 * sizeof(uint16_t));
    row8 = (uint8_t *)malloc((size_t)ow * 3);
    if (!mp || !rowbuf || !row8) { res->rc = SE_E_NOMEM; goto done; }

    {
        size_t tabs = 3 * 1536 * sizeof(int16_t) + 64;
        size_t budget = SE_LOOK_BUDGET + (size_t)ow * 4 + 4096 + fl_look_extra(ow, L);
        uint8_t *mem = (uint8_t *)se_arena_alloc(&ar, tabs + budget, 8);
        if (!mem) { res->rc = SE_E_NOMEM; goto done; }
        se_arena_init(&lookar, mem, tabs + budget);
        if (fl_build_look(&lk, &lookar, f, L, gscale, oh) != 0) { res->rc = SE_E_NOMEM; goto done; }
        for (i = 0; i < 3; i++) for (v = 0; v < 256; v++) lk.map[i][v] = (uint8_t)v;      /* the tone is made on the 16 bit light (fr_build_map) */
        (void)se_look_fit(&lk, ow, budget);
        res->look_radius = lk.hal_amount > 0 ? lk.hal_radius : 0;
        rc = se_look_begin(&c.run, &lk, ow, &lookar);
        if (rc != SE_OK) { res->rc = rc; goto done; }
    }
    {
        int16_t gen[257];                                     /* without the JPG pattern: the program's standard tone */
        for (i = 0; i <= 256; i++) gen[i] = (int16_t)fd_gen_ref(i);
        fr_build_map(mp, f, L, L->rawref ? rd->ref : gen);
    }

    c.io = io; c.base = 0; c.ow = ow; c.err = 0; c.rows = 0; c.oh = oh > 0 ? oh : 1;
    c.enc = SE_NEW(&ar, se_jpeg, 1);
    strip = SE_NEW(&ar, uint8_t, SE_JPEG_STRIP_BYTES(ow));
    if (!c.enc || !strip) { res->rc = SE_E_NOMEM; goto done; }
    memset(&ex, 0, sizeof ex);
    ex.make = meta.make[0] ? meta.make : "Canon";
    ex.model = meta.model[0] ? meta.model : "Canon EOS 5D Mark II";
    ex.software = f->full;
    ex.datetime = meta.dt;
    ex.exp_num = meta.en; ex.exp_den = meta.ed; ex.fn_num = meta.fn; ex.fn_den = meta.fd;
    ex.iso = meta.iso; ex.orientation = g.orient;
    rc = se_jpeg_begin(c.enc, ow, oh, quality, strip, &ex, fl_enc_sink, &c);
    if (rc != SE_OK) { res->rc = rc; goto done; }

    out.c = &c; out.mp = mp; out.row8 = row8; out.y = 0;
    if (g.route == 1) {
        if (fd_rs16_begin(&rs, g.cw, g.ch, ow, oh, fd_raw_emit, &out) != 0) { res->rc = FD_E_SIZE; goto done; }
        rs_open = 1;
    } else if (g.route == 2) {
        if (fd_zs_begin(&zs, g.cw, g.ch, ow, oh, 65535, fd_raw_emit, &out) != 0) { res->rc = FD_E_SIZE; goto done; }
        zs_open = 1;
    }
    fr_prepare(&fc, ri, rd->raw);
    if (div == 1) {
        if (fr_full_begin(&fc) != 0) { res->rc = SE_E_NOMEM; goto done; }
        full_open = 1;
    }
    if (g.rot) {                                              /* straightening: the band of rows the turned crop needs goes through the turning stage */
        fd_rot_emit_fn de = g.route == 1 ? fd_rs16_rows : (g.route == 2 ? fd_zs_rows : fd_raw_emit);
        void *dc = g.route == 1 ? (void *)&rs : (g.route == 2 ? (void *)&zs : (void *)&out);
        if (fd_rot_begin(&rt, g.bw, g.bh, g.cx, g.cy, g.cw, g.ch, g.rot, 65535, de, dc) != 0) { res->rc = SE_E_NOMEM; goto done; }
        rt_open = 1;
        for (y = rt.y0; y <= rt.y1 && !c.err; y++) {
            const uint16_t *row;
            if (div == 1) { fr_row_full(&fc, y, rowbuf); row = rowbuf; }
            else { int ox; for (ox = rt.bx0; ox <= rt.bx1; ox++) fr_pix_binned(&fc, ox, y, div, rowbuf + (size_t)ox * 3); row = rowbuf; }
            if (fd_rot_rows(&rt, row, 1, g.bw) != 0) c.err = 1;
        }
        if (!c.err && !fd_rot_done(&rt)) c.err = 1;
    } else
    for (y = g.cy; y < g.cy + g.ch && !c.err; y++) {          /* only the rows of the crop are made */
        const uint16_t *row;
        int bad;
        if (div == 1) { fr_row_full(&fc, y, rowbuf); row = rowbuf + (size_t)g.cx * 3; }
        else { int ox; for (ox = 0; ox < g.cw; ox++) fr_pix_binned(&fc, g.cx + ox, y, div, rowbuf + (size_t)ox * 3); row = rowbuf; }
        if (g.route == 1) bad = fd_rs16_rows(&rs, row, 1, g.cw);
        else if (g.route == 2) bad = fd_zs_rows(&zs, row, 1, g.cw);
        else bad = fd_raw_emit(&out, row, 1, g.cw);
        if (bad) c.err = 1;
    }
    if (!c.err) rc = se_look_end(c.run, fl_look_emit, &c);
    if (rc == SE_OK && !c.err) rc = se_jpeg_end(c.enc);
    if (rc == SE_OK && c.err) rc = SE_E_IO;
    res->rc = rc;
    res->mem_peak = ar.peak;
done:
    if (rs_open) fd_rs16_free(&rs);
    if (zs_open) fd_zs_free(&zs);
    if (full_open) fr_full_end(&fc);
    if (rt_open) fd_rot_free(&rt);
    free(row8); free(rowbuf); free(mp);
}

#define FD_RAW_SKIP 1            /* fd_run_raw: this file has no usable sensor data, the JPEG route goes on */
static int fd_run_raw(const uint8_t *src, size_t n, const fd_job *job, fd_buf *out, fd_result *r,
                      void (*progress)(void *ctx, int permille), void *pctx)
{
    fd_rawdata local, *rd;
    fd_src s;
    fl_io io;
    fl_result res;
    film_t f;
    size_t mw;
    int own = 0, lrc, quality = job->quality < 10 ? 10 : (job->quality > 100 ? 100 : job->quality);

    if (fd_pre_src == src && fd_pre_len == n && fd_pre_rc != SE_OK) {          /* the page has tried: do not try again at every slider */
        if (fd_pre_rc != FR_NONE) r->raw_err = fd_pre_rc;
        return FD_RAW_SKIP;
    }
    if (fd_pre && fd_pre->raw && fd_pre_src == src && fd_pre_len == n) rd = fd_pre;
    else {
        lrc = fd_raw_load(src, n, &local);
        if (lrc != SE_OK) { if (lrc != FR_NONE) r->raw_err = lrc; return FD_RAW_SKIP; }
        rd = &local; own = 1;
    }
    mw = (size_t)fd_geo_width(rd->ri.w, rd->ri.h, &job->look);
    s.p = src; s.n = n;
    memset(&io, 0, sizeof io);
    io.src_size = (uint32_t)n;
    io.read = fd_rd; io.rctx = &s;
    io.write = fd_wr; io.wctx = out;
    io.progress = progress; io.pctx = pctx;
    io.mem_size = fl_mem_needed((int)mw, 1) + fl_look_extra((int)mw, &job->look);
    io.mem = (uint8_t *)malloc(io.mem_size);
    if (!io.mem) { if (own) fd_raw_free(rd); r->rc = SE_E_NOMEM; return 0; }
    f = films[job->film];
    fd_develop_raw(&io, &f, &job->look, rd, src, n, job->size, quality, &res);
    free(io.mem);
    if (own) fd_raw_free(rd);
    r->rc = res.rc; r->raw = 1;
    r->src_w = res.src_w; r->src_h = res.src_h; r->out_w = res.out_w; r->out_h = res.out_h; r->mem_peak = res.mem_peak;
    r->pic_w = fd_pic_w; r->pic_h = fd_pic_h; r->swap = fd_pic_swap;
    if (r->rc == 0 && out->fail) r->rc = SE_E_NOMEM;
    if (r->rc != 0) { free(out->data); out->data = 0; out->len = 0; out->cap = 0; }
    return 0;
}

/* ---------------------------------------------------------------- run
 * Develops one photo (src = the whole file: a camera JPG, a CR2, a CR3 or a DNG) into out. out->data is allocated here (free() it);
 * out->len is the size of the finished JPEG. Returns r->rc. progress (may be NULL) gets 0..1000. */
static int fd_run(const uint8_t *src0, size_t srclen0, const fd_job *job, fd_buf *out, fd_result *r,
                  void (*progress)(void *ctx, int permille), void *pctx)
{
    fd_src s;
    fl_io io;
    fl_result res;
    film_t f;
    const uint8_t *src = src0;
    size_t srclen = srclen0;
    uint8_t *owned = 0;
    int sz = job->size, quality = job->quality, prc = 0;
    int tw = 0, th = 0, sw = 0, sh = 0, resize = 0, geo = 0, mw;

    memset(r, 0, sizeof *r);
    fd_pic_w = 0; fd_pic_h = 0; fd_pic_swap = 0;
    out->data = 0; out->len = 0; out->cap = 0; out->fail = 0;
    se_jpeg_set_flags(job->look.hq ? SE_JPEG_F_444 : 0);                 /* full resolution colours, or half like the camera */
    se_look_set_exact(job->look.hq ? 1 : 0);                             /* every pixel graded exactly, or by the colour table like the camera */
    if (job->film < 0 || job->film >= FILM_COUNT) { r->rc = FD_E_FILM; return r->rc; }
    if (quality < 10) quality = 10;
    if (quality > 100) quality = 100;
    if (srclen > 0xFFFFFFF0u) { r->rc = SE_E_UNSUPPORTED; return r->rc; }
    if (!job->look.usejpg && fd_run_raw(src0, srclen0, job, out, r, progress, pctx) != FD_RAW_SKIP) return r->rc;    /* the sensor data */
    fd_prepare(src0, srclen0, &src, &srclen, &owned, &prc);          /* a CR3 or a DNG becomes a plain JPEG with the metadata in it */
    if (prc) { r->rc = prc; return r->rc; }

    /* how big is the picture, to know whether a smaller size really means resizing it */
    {
        uint32_t poff = 0, plen = 0, hl = srclen < FL_HDR_BYTES ? (uint32_t)srclen : FL_HDR_BYTES;
        fl_meta meta;
        se_jdec_info ji;
        int rc = fl_find_jpeg(src, hl, (uint32_t)srclen, &poff, &plen, &meta);
        if (rc == SE_OK && (size_t)poff + plen <= srclen) {
            uint32_t jl = plen < FL_HDR_BYTES ? plen : FL_HDR_BYTES;
            rc = se_jpeg_dec_info(src + poff, jl, &ji);
            if (rc == SE_OK) { sw = ji.width; sh = ji.height; }
        }
        if (sw > 0) { int sp = meta.orient >= 5 && meta.orient <= 8; fd_pic_w = sp ? sh : sw; fd_pic_h = sp ? sw : sh; fd_pic_swap = sp; }
    }
    geo = fd_has_geo(&job->look);
    if (!geo && sz > FD_SIZE_FULL && sz < FD_SIZE_COUNT && sw > 0) {          /* the old sizes of the 5D: M and S */
        fd_target(sw, sh, sz, &tw, &th);
        resize = (tw != sw || th != sh);
    }

    s.p = src; s.n = srclen;
    memset(&io, 0, sizeof io);
    io.src_size = (uint32_t)srclen;
    io.read = fd_rd; io.rctx = &s;
    io.write = fd_wr; io.wctx = out;
    io.progress = progress; io.pctx = pctx;
    mw = fd_geo_width(sw, sh, &job->look);
    io.mem_size = fl_mem_needed(mw, 1) + fl_look_extra(mw, &job->look);
    io.mem = (uint8_t *)malloc(io.mem_size);
    if (!io.mem) { free(owned); r->rc = SE_E_NOMEM; return r->rc; }
    f = films[job->film];

    if (resize || geo)
        fd_develop_geo(&io, &f, &job->look, sz, quality, &res);
    else                                                 /* the whole picture, or one of the engine's own sizes */
        fl_develop(&io, &f, &job->look, sz > FD_SIZE_ENGINE ? sz - FD_SIZE_ENGINE : 1, quality, &res);

    free(io.mem);
    free(owned);
    r->rc = res.rc;
    r->src_w = res.src_w; r->src_h = res.src_h; r->out_w = res.out_w; r->out_h = res.out_h; r->mem_peak = res.mem_peak;
    r->pic_w = fd_pic_w; r->pic_h = fd_pic_h; r->swap = fd_pic_swap;
    if (r->rc == 0 && out->fail) r->rc = SE_E_NOMEM;
    if (r->rc != 0) { free(out->data); out->data = 0; out->len = 0; out->cap = 0; }
    return r->rc;
}

#endif
