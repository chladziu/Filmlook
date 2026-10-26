#include "look.h"
#include "se_libc.h"

/* ------------------------------------------------------------ helpers */

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* BT.601 luma, the same weights the palette conversion uses. Integer, so
 * the camera and the desktop cannot disagree. */
static int luma8(int r, int g, int b)
{
    return (77 * r + 150 * g + 29 * b) >> 8;
}

/*
 * Film grain.
 *
 * A hash of the pixel's own coordinates: repeatable (the same frame always
 * grains the same way, so the desktop and the camera produce identical
 * bytes), needs no state, and costs a handful of integer ops. Amplitude is
 * weighted by 4v(1-v) because grain is a modulation of exposed silver --
 * it vanishes in clipped whites and in blacks where there is nothing to
 * modulate, which is what stops it looking like television noise.
 *
 * This is the same construction pipeline.c uses on the raw path, kept
 * deliberately identical so a stock grains the same from either source.
 */
static unsigned grain_hash(unsigned x, unsigned y, unsigned seed)
{
    unsigned h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/*
 * 65536 / d, for the hue calculation.
 *
 * Finding a pixel's hue needs a divide by its chroma, and ARM946E-S has
 * no divide instruction -- the compiler would emit a call to
 * __aeabi_idiv for every pixel. Chroma is a byte difference, so 255
 * reciprocals cover every case and the per-pixel cost becomes one
 * multiply.
 */
static uint32_t s_recip[256];

/*
 * sin, as 8.8 fixed point, over a turn divided into 1536 -- the same
 * units hue is measured in, so a rotation indexes straight into it.
 *
 * Built here rather than taken from libm because this has to produce
 * byte-identical results on a camera with no libm at all, and because a
 * table built two different ways is a table that disagrees in the last
 * place. The Taylor series below IS the specification: tools/lookcheck.c
 * rebuilds it the same way, deliberately.
 */
static int16_t s_sin[1536];

/*
 * How much chroma survives at each luminance, 256 = all of it.
 *
 * This is the single most film-like thing in the file, and the most
 * obviously absent one. Real emulsion loses colour toward both ends: at
 * the top all three dye layers run out together, so a bright colour
 * desaturates on its way to white, and at the bottom there is too little
 * exposure left to hold a hue. Digital saturation does neither -- it
 * keeps a colour at full strength right up to clipping, which is exactly
 * what makes an oversaturated frame read as a slider rather than a film.
 *
 * Measured on a frame through VELVIA: foliage at luma 168 went from
 * chroma 47 to 100, a 2.1x boost held at full strength into the
 * highlights, and came back looking like neon.
 *
 * Flat through the midtones, where film is at its most colourful, and
 * rolling off above and below.
 */
static uint8_t s_chroma[256];

static void tables_init(void)
{
    int i;
    if (s_recip[255]) return;                 /* already built */
    for (i = 1; i < 256; i++) s_recip[i] = (uint32_t)(65536 / i);

    for (i = 0; i < 256; i++) {
        int w;
        if (i < 24)       w = 150 + (i * 106) / 24;        /* 150 -> 256  */
        else if (i <= 112) w = 256;                         /* the midtones */
        else              w = 256 - ((i - 112) * 140) / 143; /* 256 -> 116 */
        s_chroma[i] = (uint8_t)(w < 0 ? 0 : (w > 255 ? 255 : w));
    }

    for (i = 0; i < 1536; i++) {
        /* fold into the first quarter turn, then Taylor to x^7 in 16.16 fixed point (no floating point in a module) */
        int q = i & 1535, sign = 1, v;
        int x, x2;
        if (q >= 768) { q -= 768; sign = -1; }
        if (q > 384) q = 768 - q;
        x  = q * 268;                                   /* q * pi / 768 in 16.16 (268.08) */
        x2 = (int)(((long long)x * x) >> 16);
        v  = 65536 - x2 / 42;
        v  = 65536 - (int)(((long long)(x2 / 20) * v) >> 16);
        v  = 65536 - (int)(((long long)(x2 / 6) * v) >> 16);
        v  = (int)(((long long)x * v) >> 16);           /* sin(x) in 16.16 */
        v  = sign * ((v + 128) >> 8);                   /* 8.8 fixed point */
        s_sin[i] = (int16_t)v;
    }
}

/* a is within -1536 .. 3071: no modulo (a soft divide per pixel on a core without a divide instruction) */
static inline int sin_at(int a)
{
    if (a < 0) a += 1536;
    if (a >= 1536) a -= 1536;
    return s_sin[a];
}
#define SE_SIN(a) sin_at(a)
#define SE_COS(a) sin_at((a) + 384)

/*
 * Which hue a pixel is, as 0..1535 -- 256 per sector, R Y G C B M.
 * Returns -1 for a pixel with no colour in it at all.
 */
static int hue1536(int r, int g, int b, int *chroma)
{
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int d  = mx - mn, h;

    *chroma = d;
    if (d <= 0) return -1;
    if (mx == r)      h = ((g - b) * (int)s_recip[d]) >> 8;
    else if (mx == g) h = (((b - r) * (int)s_recip[d]) >> 8) + 512;
    else              h = (((r - g) * (int)s_recip[d]) >> 8) + 1024;
    if (h < 0)     h += 1536;
    if (h >= 1536) h -= 1536;
    return h;
}

/*
 * 256 * num / den without a divide, for den up to about 1020.
 * s_recip only covers 1..255, so halve both until it fits -- at most two
 * shifts, and the precision lost is far below one output level.
 */
static int fit_scale(int num, int den)
{
    while (den > 255) { den >>= 1; num >>= 1; }
    if (den <= 0) return 256;
    return (num * (int)s_recip[den]) >> 8;
}

/*
 * Fit a colour back inside 0..255 by pulling its CHROMA in, not by
 * clipping its channels.
 *
 * This is the difference between a film look and a saturation slider.
 * Clipping takes a channel to 0 or 255 and leaves the others where they
 * are, which produces the purest, most electric version of that hue with
 * every trace of detail gone -- flat neon foliage, flat neon sky. Film
 * cannot do it: the three dye layers overlap, so a density of zero in one
 * layer while the others hold is not a state the material has.
 *
 * Measured on one frame through VELVIA: 57.5% of pixels had a channel at
 * the floor, against 27.8% with no look at all, and the greens came back
 * as a single flat colour.
 *
 * So find the largest scale that keeps every channel in range and apply
 * it to all three. Hue and luminance survive; only the saturation gives,
 * and only by as much as it must.
 */
static void gamut_fit(int *rr, int *gg, int *bb)
{
    int r = *rr, g = *gg, b = *bb;
    int y, sc = 256, d;

    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255)
        return;                       /* already fits: the common case */

    y = (77 * r + 150 * g + 29 * b) >> 8;
    if (y < 0)   y = 0;
    if (y > 255) y = 255;

    d = r - y;
    if (r > 255 && d > 0) { int t = fit_scale(255 - y, d);  if (t < sc) sc = t; }
    if (r < 0   && d < 0) { int t = fit_scale(y, -d);       if (t < sc) sc = t; }
    d = g - y;
    if (g > 255 && d > 0) { int t = fit_scale(255 - y, d);  if (t < sc) sc = t; }
    if (g < 0   && d < 0) { int t = fit_scale(y, -d);       if (t < sc) sc = t; }
    d = b - y;
    if (b > 255 && d > 0) { int t = fit_scale(255 - y, d);  if (t < sc) sc = t; }
    if (b < 0   && d < 0) { int t = fit_scale(y, -d);       if (t < sc) sc = t; }

    if (sc < 0)   sc = 0;
    if (sc > 256) sc = 256;
    *rr = y + (((r - y) * sc) >> 8);
    *gg = y + (((g - y) * sc) >> 8);
    *bb = y + (((b - y) * sc) >> 8);
}

/* ------------------------------------------------------- the run ----- */

static int smooth256(int t);
static int gnoise(unsigned gx, unsigned gy, unsigned seed);

/* one layer of the film grain (see noise_layer): everything that does not change from row to row is made once */
typedef struct {
    uint16_t *tx, *gi;        /* per pixel: the smooth weight between two lattice points (0..256) and the left lattice point */
    int      *n0, *n1, *vv;   /* noise of the lattice row gy and gy + 1, and the blend of both for the row that is made now */
    int       nl, gyc;        /* lattice points in a row, the gy of n0 (-1 = none yet) */
    unsigned  inc, seed;
    int       weight;
} gl_t;

static int grain_layer_begin(gl_t *g, se_arena *ar, int w, unsigned inc, unsigned seed, int weight);

/* Colour table: the whole per-pixel stage (hue, saturation, split tone, chroma rolloff, gamut) depends only on the colour the curves
 * produce, so it is evaluated once for LUT_N^3 colours per photo (with the original code, grade_row_direct) and every pixel then
 * interpolates between them (tetrahedral, like a cube LUT of a colourist's software): about 70 instead of 250 instructions. */
#ifndef LUT_N
#define LUT_N      25
#endif
#define LUT_BYTES  ((size_t)LUT_N * LUT_N * LUT_N * 3)
static void lut_build(uint8_t *lut, const se_look *lk);

struct gp_col { uint8_t lx, c, ws, wo; int8_t nb; };     /* lx: texel in the cell, c: cell + 1, nb: the neighbour cell (-1, 0, 1), ws / wo: weights of the cell and its neighbour (256 = 1.0) */

/* the most cells of a plate walk across a row: 5616 px at 4 texels per pixel is 88 */
#define SE_GP_CELLS 112

struct se_look_run {
    se_look  lk;
    int      w;
    uint8_t *lut;             /* LUT_N^3 x 3 bytes, r slowest, b fastest; NULL = grade every pixel directly */
    int      y_out;           /* rows emitted                            */

    int      on;              /* halation enabled: a delay line is live  */
    int      r;               /* halation radius, in rows                */
    int      nring;           /* 2r + 1: the mask window                 */
    int      ndelay;          /* r + 1: see se_look_begin                 */
    uint8_t *ring;            /* nring rows of horizontally blurred mask */
    uint8_t *delay;           /* ndelay rows of graded RGB               */
    int     *colsum;          /* w running column sums over the ring     */
    int      pushed;          /* mask rows pushed, priming included      */
    uint8_t *scratch;         /* one row of mask, pre-blur               */

    uint8_t *flat;            /* !on only: the one row we grade into     */
    int     *gn;              /* grain: the noise of one row              */
    gl_t     gl[2];           /* grain: the coarse and the fine layer     */
    int      ngl;
    struct gp_col *gp_c;      /* plate grain: per column, where it is in its cell and how it blends with the neighbour cell */
    int      gp_ncx;          /* cells across */
    int      gp_j;            /* cell row the hashes below are for */
    unsigned gp_h[3][SE_GP_CELLS + 2];                     /* the random numbers of the cells of the row above, this row and the row below (index c + 1) */
    uint16_t gp_T[256];       /* tone weight x amplitude: the grain of a texel value t is t * T[v] / 2^16 */
    const int8_t *gp_rp[2][SE_GP_CELLS + 2];               /* this row: per cell (index c + 1) the plate row it reads; [0] the cell row of the row itself, [1] its neighbour */
    uint8_t  gp_ox[2][SE_GP_CELLS + 2], gp_xm[2][SE_GP_CELLS + 2];  /* ... and the shift and the mirror (255 / 0) across */

    int     *vx2;             /* vignette: squared horizontal distance of every column, pre-shifted */
    int      vshift;          /* right shift that brings the squared distance below 65536 */
    int      vscale;          /* 255 * 65536 / (max squared distance >> vshift) */
};

/* the weights of a texel `l` of a cell of `n` texels near the border of its neighbour (SE_GP_BLEND texels each side):
 * sin and cos of 45 degrees + 45 degrees * (distance from the border + 1/2) / SE_GP_BLEND, so that the two neighbours, which
 * are independent grain, keep the rms of the grain across the border (ws^2 + wo^2 = 1). Inside the cell: wo = 0. */
static void gp_blend(unsigned l, unsigned n, int8_t *nb, uint8_t *ws, uint8_t *wo)
{
    static const uint8_t tws[SE_GP_BLEND] = { 190, 206, 220, 231, 241, 248, 253, 255 };
    static const uint8_t two[SE_GP_BLEND] = { 172, 152, 132, 109, 86, 62, 38, 13 };
    unsigned d;
    *nb = 0; *ws = 255; *wo = 0;
    if (l < SE_GP_BLEND) { *nb = -1; d = l; }
    else if (l >= n - SE_GP_BLEND) { *nb = 1; d = n - 1 - l; }
    else return;
    *ws = tws[d]; *wo = two[d];
}

/* cells across a row of `width` pixels when the plate is walked with `stride` (16.16) */
static int gp_cells(unsigned stride, int width)
{
    return (int)((((unsigned)(width - 1) * stride) >> 16) / SE_GP_W) + 1;
}

/* lattice points of one grain layer in a row of `width` pixels */
static int grain_lattice(unsigned inc, int width) { return (int)(((unsigned)(width - 1) * inc) >> 16) + 2; }

static size_t grain_layer_bytes(int inc, int width)
{
    return (size_t)width * 2 * sizeof(uint16_t) + (size_t)grain_lattice((unsigned)inc, width) * 3 * sizeof(int) + 64;
}

size_t se_look_bytes(const se_look *lk, int width)
{
    size_t n;
    size_t vig = lk->vig_amount > 0 ? (size_t)width * sizeof(int) + 16 : 0;
    if (lk->grain > 0 && lk->gp) vig += (size_t)width * sizeof(struct gp_col) + 16;
    else if (lk->grain > 0) vig += (size_t)width * sizeof(int) + 16 + grain_layer_bytes(lk->grain_inc_a, width) +
                              (lk->grain_w_b > 0 ? grain_layer_bytes(lk->grain_inc_b, width) : 0);
    vig += LUT_BYTES + 16;                                           /* the colour table */
    if (lk->hal_amount <= 0)
        return sizeof(struct se_look_run) + (size_t)width * 3 + 32 + vig;
    n  = sizeof(struct se_look_run) + 16 + vig;
    n += (size_t)(2 * lk->hal_radius + 1) * (size_t)width;          /* ring   */
    n += (size_t)(lk->hal_radius + 1) * (size_t)width * 3;          /* delay  */
    n += (size_t)width * sizeof(int);                               /* colsum */
    n += (size_t)width;                                             /* scratch*/
    return n + 64;
}

size_t se_look_fit(se_look *lk, int width, size_t budget)
{
    size_t n = se_look_bytes(lk, width);

    while (lk->hal_amount > 0 && lk->hal_radius > 1 && n > budget) {
        lk->hal_radius--;
        n = se_look_bytes(lk, width);
    }
    if (n > budget && lk->hal_amount > 0) {
        /* A radius of 1 is not a glow anyway, and with amount 0 the run
         * allocates no delay line at all -- so this always terminates at
         * something that fits. */
        lk->hal_amount = 0;
        n = se_look_bytes(lk, width);
    }
    return n;
}

se_err se_look_begin(se_look_run **out, const se_look *lk,
                     int width, se_arena *ar)
{
    se_look_run *r;

    if (!out || !lk || width <= 0 || !ar) return SE_E_CORRUPT;

    /*
     * Also here, not only in se_look_from_stock.
     *
     * A caller may fill se_look itself rather than resolving it from a
     * stock -- tools/lookcheck.c does exactly that -- and with the table
     * still zero every pixel reports hue 0 and the whole per-hue stage
     * silently collapses onto the red sector. It cost an afternoon
     * looking for an off-by-one in the reciprocal. Every consumer has to
     * come through here, so this is where it belongs.
     */
    tables_init();

    r = SE_NEW(ar, se_look_run, 1);
    if (!r) return SE_E_NOMEM;
    memset(r, 0, sizeof *r);
    r->lk = *lk;
    r->w  = width;
#ifdef SE_NO_LUT                                             /* (PC tests only: the old way, to compare with) */
    r->lut = 0;
#else
    r->lut = SE_NEW(ar, uint8_t, LUT_BYTES);                 /* (a failed allocation only means: grade every pixel directly) */
    if (r->lut) lut_build(r->lut, &r->lk);
#endif

    if (lk->hal_amount > 0) {
        r->r      = lk->hal_radius;
        r->nring  = 2 * r->r + 1;
        /*
         * The mask window spans 2r+1 rows, but the graded RGB does not.
         * A row is written at push index i and read back at push index
         * i+r, so it only has to survive r+1 pushes -- it is overwritten
         * at i+r+1, one push after it was needed. Sizing this ring like
         * the mask ring held r extra rows of RGB for nothing: 120 KB at
         * full width and radius 16, which is the difference between the
         * full-size develop getting radius 8 and radius 13.
         */
        r->ndelay = r->r + 1;
        r->ring    = SE_NEW(ar, uint8_t, (size_t)r->nring * width);
        r->delay   = SE_NEW(ar, uint8_t, (size_t)r->ndelay * width * 3);
        r->colsum  = SE_NEW(ar, int, width);
        r->scratch = SE_NEW(ar, uint8_t, width);
        if (!r->ring || !r->delay || !r->colsum || !r->scratch)
            return SE_E_NOMEM;
        memset(r->ring,   0, (size_t)r->nring * width);
        memset(r->colsum, 0, (size_t)width * sizeof(int));
        r->on = 1;
    } else {
        r->flat = SE_NEW(ar, uint8_t, (size_t)width * 3);
        if (!r->flat) return SE_E_NOMEM;
    }
    if (lk->grain > 0 && lk->gp && lk->gp_stride >= 65536u && gp_cells(lk->gp_stride, width) <= SE_GP_CELLS) {
        int x, v;
        r->gp_c = SE_NEW(ar, struct gp_col, width);
        if (!r->gp_c) return SE_E_NOMEM;
        r->gp_ncx = gp_cells(lk->gp_stride, width);
        r->gp_j = -1;
        for (x = 0; x < width; x++) {
            unsigned u = ((unsigned)x * lk->gp_stride) >> 16, lx = u % SE_GP_W;
            struct gp_col *g = &r->gp_c[x];
            g->lx = (uint8_t)lx; g->c = (uint8_t)(u / SE_GP_W + 1);
            gp_blend(lx, SE_GP_W, &g->nb, &g->ws, &g->wo);
        }
        for (v = 0; v < 256; v++) r->gp_T[v] = (uint16_t)(((unsigned)lk->gp_amp * lk->gp_tone[v] + 8) >> 4);
    } else if (lk->grain > 0) {
        r->gn = SE_NEW(ar, int, width);
        if (!r->gn) return SE_E_NOMEM;
        r->ngl = lk->grain_w_b > 0 ? 2 : 1;
        if (grain_layer_begin(&r->gl[0], ar, width, (unsigned)lk->grain_inc_a, 0x5EEDu, lk->grain_w_a)) return SE_E_NOMEM;
        if (r->ngl > 1 && grain_layer_begin(&r->gl[1], ar, width, (unsigned)lk->grain_inc_b, 0xB10Cu, lk->grain_w_b)) return SE_E_NOMEM;
    }
    if (lk->vig_amount > 0 && lk->vig_h > 1 && width > 1) {
        int x, h = lk->vig_h;
        unsigned maxd2;
        r->vx2 = SE_NEW(ar, int, width);
        if (!r->vx2) return SE_E_NOMEM;
        /* squared distance from the centre, in half pixels: dx = 2x - (w-1), dy = 2y - (h-1) */
        maxd2 = (unsigned)(width - 1) * (unsigned)(width - 1) + (unsigned)(h - 1) * (unsigned)(h - 1);
        r->vshift = 0;
        while ((maxd2 >> r->vshift) > 65535u) r->vshift++;
        r->vscale = (255 * 65536) / (int)(maxd2 >> r->vshift ? (maxd2 >> r->vshift) : 1);
        for (x = 0; x < width; x++) {
            int dx = 2 * x - (width - 1);
            r->vx2[x] = (int)(((unsigned)(dx * dx)) >> r->vshift);
        }
    }
    *out = r;
    return SE_OK;
}

/* Curves, saturation and split tone: everything that needs only the pixel
 * itself. Writes in place. */
static void grade_row_direct(const se_look *lk, uint8_t *row, int w)
{
    int x;
    for (x = 0; x < w; x++) {
        int rr = lk->map[0][row[x * 3 + 0]];
        int gg = lk->map[1][row[x * 3 + 1]];
        int bb = lk->map[2][row[x * 3 + 2]];

        /*
         * The per-hue stage, on the colour the curves produced.
         *
         * Saturation, rotation and luminance all act on the same two
         * numbers, so this works in luma + chroma rather than in RGB:
         * u and v ARE the colour, rotating them IS a hue shift, scaling
         * them IS saturation, and y on its own IS how bright that hue
         * renders. Doing it in RGB would need three separate passes and
         * could not express a rotation at all.
         *
         * Everything is derived from luma8's own weights, so the inverse
         * below is exact up to luma8's truncation -- which is why the
         * whole block is skipped when a stock asks for nothing per-hue,
         * rather than relying on it round-tripping to the same bytes.
         */
        if (lk->hue_any) {
            int chroma;
            int h = hue1536(rr, gg, bb, &chroma);
            if (h >= 0) {
                int msat = lk->hs[h];
                int mlum = lk->hl[h];
                int mrot = lk->hr[h];
                int y = luma8(rr, gg, bb);
                int u = bb - y, v = rr - y;

                /*
                 * Fade the whole stage out as the pixel approaches
                 * neutral. See SE_HUE_FULL: the hue of a near-grey pixel
                 * is meaningless, and acting on it tears flat areas into
                 * blotches.
                 */
                if (chroma < SE_HUE_FULL) {
                    int wt = (chroma * 1638) >> 8;           /* chroma * 256 / 40 */
                    msat = 256 + (((msat - 256) * wt) >> 8);
                    mlum = 256 + (((mlum - 256) * wt) >> 8);
                    mrot = (mrot * wt) >> 8;
                }

                if (mrot) {
                    int cs = SE_COS(mrot), sn = SE_SIN(mrot);
                    int u2 = (u * cs - v * sn) >> 8;
                    int v2 = (u * sn + v * cs) >> 8;
                    u = u2; v = v2;
                }
                if (msat != 256) {
                    u = (u * msat) >> 8;
                    v = (v * msat) >> 8;
                }
                if (mlum != 256) y = (y * mlum) >> 8;

                rr = y + v;
                bb = y + u;
                gg = y - (((77 * v + 29 * u) * 437) >> 16);      /* / 150 */
            }
        }

        if (lk->sat != 256) {
            int y = luma8(clamp8(rr), clamp8(gg), clamp8(bb));
            rr = y + (((rr - y) * lk->sat) >> 8);
            gg = y + (((gg - y) * lk->sat) >> 8);
            bb = y + (((bb - y) * lk->sat) >> 8);
        }

        if (lk->sh[0] | lk->sh[1] | lk->sh[2] |
            lk->hi[0] | lk->hi[1] | lk->hi[2]) {
            int y  = luma8(clamp8(rr), clamp8(gg), clamp8(bb));
            int wl = 255 - y;                  /* shadow weight    */
            rr += (lk->sh[0] * wl + lk->hi[0] * y) >> 8;
            gg += (lk->sh[1] * wl + lk->hi[1] * y) >> 8;
            bb += (lk->sh[2] * wl + lk->hi[2] * y) >> 8;
        }

        /*
         * Film's own chroma rolloff, applied once to the finished
         * colour -- see s_chroma. Before the gamut fit, because it
         * removes most of what would otherwise need fitting.
         */
        {
            int y = (77 * rr + 150 * gg + 29 * bb) >> 8;
            int w;
            if (y < 0)   y = 0;
            if (y > 255) y = 255;
            w = s_chroma[y];
            if (w != 256) {
                rr = y + (((rr - y) * w) >> 8);
                gg = y + (((gg - y) * w) >> 8);
                bb = y + (((bb - y) * w) >> 8);
            }
        }

        gamut_fit(&rr, &gg, &bb);
        row[x * 3 + 0] = (uint8_t)clamp8(rr);
        row[x * 3 + 1] = (uint8_t)clamp8(gg);
        row[x * 3 + 2] = (uint8_t)clamp8(bb);
    }
}

/* the colour table of this photo: grade_row_direct on the LUT_N^3 colours (the curves are applied outside, so the nodes are the colours
 * AFTER the curves; the node k stands for the value k * 255 / (LUT_N - 1)) */
static void lut_build(uint8_t *lut, const se_look *lk)
{
    se_look id = *lk;
    uint8_t row[LUT_N * 3];
    int c, v, ri, gi, bi;
    for (c = 0; c < 3; c++) for (v = 0; v < 256; v++) id.map[c][v] = (uint8_t)v;
    for (ri = 0; ri < LUT_N; ri++)
        for (gi = 0; gi < LUT_N; gi++) {
            for (bi = 0; bi < LUT_N; bi++) {
                row[bi * 3 + 0] = (uint8_t)((ri * 255 + (LUT_N - 1) / 2) / (LUT_N - 1));
                row[bi * 3 + 1] = (uint8_t)((gi * 255 + (LUT_N - 1) / 2) / (LUT_N - 1));
                row[bi * 3 + 2] = (uint8_t)((bi * 255 + (LUT_N - 1) / 2) / (LUT_N - 1));
            }
            grade_row_direct(&id, row, LUT_N);
            memcpy(lut + ((size_t)(ri * LUT_N + gi) * LUT_N) * 3, row, (size_t)LUT_N * 3);
        }
}

/* the same as grade_row_direct, from the table: curves, then tetrahedral interpolation. The position of a value c on the table is
 * c * (LUT_N - 1) / 255 in 1/256 of a node: (c * (LUT_N - 1) * 257) >> 8 (255 -> the last node exactly). */
static void grade_row_lut(const se_look *lk, const uint8_t *lut, uint8_t *row, int w)
{
    const unsigned K = (LUT_N - 1) * 257;
    int x;
    for (x = 0; x < w; x++) {
        unsigned pr = ((unsigned)lk->map[0][row[x * 3 + 0]] * K) >> 8;
        unsigned pg = ((unsigned)lk->map[1][row[x * 3 + 1]] * K) >> 8;
        unsigned pb = ((unsigned)lk->map[2][row[x * 3 + 2]] * K) >> 8;
        unsigned fr = pr & 255, fg = pg & 255, fb = pb & 255;
        const uint8_t *n0 = lut + ((((pr >> 8) * LUT_N + (pg >> 8)) * LUT_N + (pb >> 8)) * 3);
        /* the three axes, the biggest fraction first: node offsets 867 (r), 51 (g), 3 (b) */
        unsigned f1, f2, f3, o1, o2;
        const unsigned sr = LUT_N * LUT_N * 3, sg = LUT_N * 3, sb = 3;
        if (fr >= fg) {
            if (fg >= fb)      { f1 = fr; f2 = fg; f3 = fb; o1 = sr; o2 = sg; }
            else if (fr >= fb) { f1 = fr; f2 = fb; f3 = fg; o1 = sr; o2 = sb; }
            else               { f1 = fb; f2 = fr; f3 = fg; o1 = sb; o2 = sr; }
        } else {
            if (fr >= fb)      { f1 = fg; f2 = fr; f3 = fb; o1 = sg; o2 = sr; }
            else if (fg >= fb) { f1 = fg; f2 = fb; f3 = fr; o1 = sg; o2 = sb; }
            else               { f1 = fb; f2 = fg; f3 = fr; o1 = sb; o2 = sg; }
        }
        {
            const uint8_t *na = n0 + o1, *nb = na + o2, *nc = n0 + (sr + sg + sb);
            int k;
            for (k = 0; k < 3; k++) {
                int v = ((int)n0[k] << 8) + (int)f1 * ((int)na[k] - (int)n0[k]) + (int)f2 * ((int)nb[k] - (int)na[k]) + (int)f3 * ((int)nc[k] - (int)nb[k]);
                row[x * 3 + k] = (uint8_t)((v + 128) >> 8);
            }
        }
    }
}

#ifdef SE_LUT_CHECK                                    /* PC test: how far is the table from the direct grading? */
#include <stdio.h>
#include <stdlib.h>
static long lc_n, lc_sum, lc_max, lc_hist[8];
static void lc_report(void)
{
    fprintf(stderr, "LUT check: %ld values, mean abs error %.3f, max %ld, error 0:%ld 1:%ld 2:%ld 3:%ld 4+:%ld\n", lc_n, (double)lc_sum / (double)lc_n,
            lc_max, lc_hist[0], lc_hist[1], lc_hist[2], lc_hist[3], lc_hist[4] + lc_hist[5] + lc_hist[6] + lc_hist[7]);
}
#endif
static void grade_row(const se_look *lk, const uint8_t *lut, uint8_t *row, int w)
{
    if (!lut) { grade_row_direct(lk, row, w); return; }
#ifdef SE_LUT_CHECK
    {
        static int reg;
        uint8_t *ref = (uint8_t *)malloc((size_t)w * 3);
        int i;
        if (!reg) { reg = 1; atexit(lc_report); }
        memcpy(ref, row, (size_t)w * 3);
        grade_row_direct(lk, ref, w);
        grade_row_lut(lk, lut, row, w);
        for (i = 0; i < w * 3; i++) {
            long e = row[i] > ref[i] ? row[i] - ref[i] : ref[i] - row[i];
            lc_n++; lc_sum += e; if (e > lc_max) lc_max = e; lc_hist[e > 7 ? 7 : e]++;
        }
        free(ref);
        return;
    }
#endif
    grade_row_lut(lk, lut, row, w);
}

/*
 * The highlight mask for one graded row, blurred horizontally.
 *
 * A running sum, so the cost per pixel does not depend on the radius --
 * one add and one subtract however wide the glow is. The mask is how far
 * the pixel is above the threshold, scaled so that a pixel at 255 is 255:
 * a soft knee would be prettier, but this is what a threshold means and it
 * is one comparison.
 */
static void mask_row(const se_look *lk, const uint8_t *row, int w,
                     uint8_t *scratch, uint8_t *dst)
{
    int x, rad = lk->hal_radius, win = 2 * rad + 1;
    int sum = 0;
    int span = 255 - lk->hal_thresh;
    int rs, rw;
    if (span < 1) span = 1;
    rs = (int)s_recip[span] * 255;               /* 255 * 65536 / span, v <= span so v * rs <= 255 * 65536 */
    rw = 65536 / win + 1;                        /* one divide per row */

    for (x = 0; x < w; x++) {
        int y = luma8(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]);
        int v = y - lk->hal_thresh;
        scratch[x] = (uint8_t)(v <= 0 ? 0 : ((v * rs) >> 16));
    }

    /* prime the window with the left edge clamped */
    for (x = -rad; x <= rad; x++)
        sum += scratch[x < 0 ? 0 : (x >= w ? w - 1 : x)];
    for (x = 0; x < w; x++) {
        int add = x + rad + 1, sub = x - rad;
        int m = (sum * rw) >> 16;
        dst[x] = (uint8_t)(m > 255 ? 255 : m);
        sum += scratch[add >= w ? w - 1 : add];
        sum -= scratch[sub < 0 ? 0 : sub];
    }
}

/* Add the glow to one row, given its vertical column sums. */
static void add_glow(const se_look *lk, uint8_t *row, int w,
                     const int *colsum, int nrows)
{
    int x;
    int rn;
    if (nrows < 1) nrows = 1;
    rn = 65536 / nrows + 1;                     /* one divide per row */
    for (x = 0; x < w; x++) {
        int g = (colsum[x] * rn) >> 16;         /* 0..255 */
        int a, room;
        if (g <= 0) continue;
        a = (g * lk->hal_amount * 656) >> 16;   /* / 100 */

        /*
         * Fade the glow out where the frame is already bright.
         *
         * Halation is light that scattered off the base and exposed the
         * emulsion a second time. Where the emulsion is already near
         * saturation that second exposure changes nothing, so the halo is
         * only ever visible bleeding into something darker.
         *
         * Adding it everywhere is what made this unusable in daylight: a
         * white building lit the whole mask, the glow landed on top of
         * the building as well as beside it, and the frame came out
         * outlined in red and washed pink. Weighting by the headroom left
         * in the destination fixes both ends at once -- a street light
         * against a dark wall still blooms hard, and a white wall in
         * sunlight gets almost nothing, which is what film does.
         */
        room = 255 - luma8(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]);
        a = (a * room * 257) >> 16;             /* / 255 */
        if (a <= 0) continue;

        row[x * 3 + 0] = (uint8_t)clamp8(row[x * 3 + 0] + ((a * lk->hal_tint[0]) >> 8));
        row[x * 3 + 1] = (uint8_t)clamp8(row[x * 3 + 1] + ((a * lk->hal_tint[1]) >> 8));
        row[x * 3 + 2] = (uint8_t)clamp8(row[x * 3 + 2] + ((a * lk->hal_tint[2]) >> 8));
    }
}

/*
 * Grain: soft, round and a little uneven, like silver halide seen through a scanner -- not square blocks.
 *
 * Two layers of "value noise": a lattice of random numbers (from the coordinate hash, so the same frame always grains
 * the same way) is interpolated smoothly (smoothstep) between its points, which makes round blobs about one lattice
 * step wide. The coarse layer sets the size of the grain (GrainSize), the fine layer, a little more than half as wide,
 * makes it rougher (GrainFrequency). The lattice moves by `inc` per pixel (16.16), so the grain keeps its size relative
 * to the frame whatever size the frame is delivered at. The result is one number per pixel, the same for all channels
 * (monochrome grain), applied with the weight 4v(1-v) so it fades out in clipped whites and in the deepest blacks.
 */
static int smooth256(int t)                     /* t 0..255 -> 0..256, 3t^2 - 2t^3 */
{
    return (t * t * (768 - 2 * t)) >> 16;
}

static int gnoise(unsigned gx, unsigned gy, unsigned seed)
{
    return (int)(grain_hash(gx, gy, seed) & 0xFFFF) - 32768;
}

/* a layer is made once per frame: the weights between the lattice points of every pixel do not change from row to row */
static int grain_layer_begin(gl_t *g, se_arena *ar, int w, unsigned inc, unsigned seed, int weight)
{
    int x;
    g->inc = inc; g->seed = seed; g->weight = weight; g->gyc = -1;
    g->nl = grain_lattice(inc, w);
    g->tx = SE_NEW(ar, uint16_t, w);
    g->gi = SE_NEW(ar, uint16_t, w);
    g->n0 = SE_NEW(ar, int, g->nl);
    g->n1 = SE_NEW(ar, int, g->nl);
    g->vv = SE_NEW(ar, int, g->nl);
    if (!g->tx || !g->gi || !g->n0 || !g->n1 || !g->vv) return 1;
    for (x = 0; x < w; x++) {
        unsigned fx = (unsigned)x * inc;
        g->gi[x] = (uint16_t)(fx >> 16);
        g->tx[x] = (uint16_t)smooth256((int)((fx >> 8) & 255));
    }
    return 0;
}

/* the lattice of row y: the noise of the lattice rows gy and gy + 1 (kept from the row before when gy did not change, or
 * moved by one), blended for the position of y between them */
static void grain_layer_row(gl_t *g, int y)
{
    unsigned fy = (unsigned)y * g->inc;
    int gy = (int)(fy >> 16), ty = smooth256((int)((fy >> 8) & 255)), i, nl = g->nl;
    if (gy != g->gyc) {
        if (g->gyc >= 0 && gy == g->gyc + 1) { int *s = g->n0; g->n0 = g->n1; g->n1 = s; }
        else for (i = 0; i < nl; i++) g->n0[i] = gnoise((unsigned)i, (unsigned)gy, g->seed);
        for (i = 0; i < nl; i++) g->n1[i] = gnoise((unsigned)i, (unsigned)gy + 1, g->seed);
        g->gyc = gy;
    }
    for (i = 0; i < nl; i++) { int a = g->n0[i]; g->vv[i] = a + (((g->n1[i] - a) * ty) >> 8); }
}

static void grain_row(const se_look *lk, se_look_run *r, uint8_t *row, int w, int y)
{
    int x;
    /* 0..100 -> about +-15 at full strength. */
    int amp = (lk->grain * SE_GRAIN_AMP) / 100;
    gl_t *A = &r->gl[0], *B = &r->gl[1];
    if (amp <= 0 || !r->gn) return;
    grain_layer_row(A, y);
    if (r->ngl > 1) grain_layer_row(B, y);
    for (x = 0; x < w; x++) {
        int g = A->gi[x], a = A->vv[g], n = ((a + (((A->vv[g + 1] - a) * A->tx[x]) >> 8)) * A->weight) >> 8;
        int d, v, wgt;
        if (r->ngl > 1) {
            g = B->gi[x]; a = B->vv[g];
            n += ((a + (((B->vv[g + 1] - a) * B->tx[x]) >> 8)) * B->weight) >> 8;
        }
        d = (n * amp) >> 15;
        if (!d) continue;
        /* 4v(1-v), in eighths, so grain fades out at both ends */
        v = row[x * 3 + 0]; wgt = (v * (255 - v)) >> 6; row[x * 3 + 0] = (uint8_t)clamp8(v + ((d * wgt) >> 8));
        v = row[x * 3 + 1]; wgt = (v * (255 - v)) >> 6; row[x * 3 + 1] = (uint8_t)clamp8(v + ((d * wgt) >> 8));
        v = row[x * 3 + 2]; wgt = (v * (255 - v)) >> 6; row[x * 3 + 2] = (uint8_t)clamp8(v + ((d * wgt) >> 8));
    }
}

/* Grain from a plate. The plate is walked in its own texel space: column x is at texel u = x * stride, row y at v = y * stride.
 * That space is cut into cells of SE_GP_W x SE_GP_H texels, and every cell reads the (cyclic) plate with a shift and a mirror of
 * its own: the plate has no seam inside a cell, and the cells differ from each other, so the eye finds no repeating pattern.
 * Because the plate is cyclic, a cell can also be read a little beyond its own border: that is what the blend near a border uses. */
static void gp_cells_row(se_look_run *r, const se_look *lk, int k, int j, unsigned ly)
{
    int c;
    const unsigned *h = r->gp_h[j - r->gp_j + 1];                /* the row above, the row itself or the row below */
    for (c = 0; c < r->gp_ncx + 2; c++) {
        unsigned yy = (h[c] & 0x200) ? (SE_GP_H - 1 - ly) : ly;
        r->gp_ox[k][c] = (uint8_t)h[c];
        r->gp_xm[k][c] = (h[c] & 0x100) ? 255 : 0;
        r->gp_rp[k][c] = lk->gp + (size_t)((yy + (h[c] >> 10)) % SE_GP_H) * SE_GP_W * 3;
    }
}

/* the grain of the texel of column g, in cell row set k, as three signed bytes (blended with the next cell across when near a border) */
static void gp_samp(const se_look_run *r, int k, const struct gp_col *g, int *o)
{
    int c = g->c;
    const int8_t *a = r->gp_rp[k][c] + (size_t)(((g->lx ^ r->gp_xm[k][c]) + r->gp_ox[k][c]) & 255) * 3;
    if (!g->wo) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; return; }
    {
        int cn = c + g->nb;
        const int8_t *b = r->gp_rp[k][cn] + (size_t)(((g->lx ^ r->gp_xm[k][cn]) + r->gp_ox[k][cn]) & 255) * 3;
        o[0] = (g->ws * a[0] + g->wo * b[0]) >> 8;
        o[1] = (g->ws * a[1] + g->wo * b[1]) >> 8;
        o[2] = (g->ws * a[2] + g->wo * b[2]) >> 8;
    }
}

static void grain_row_plate(const se_look *lk, se_look_run *r, uint8_t *row, int w, int y)
{
    unsigned v = ((unsigned)y * lk->gp_stride) >> 16, ly = v % SE_GP_H;
    int j = (int)(v / SE_GP_H), x;
    int8_t nby = 0;
    uint8_t wys = 255, wyo = 0;
    const uint16_t *T = r->gp_T;
    if (lk->gp_amp <= 0) return;
    if (j != r->gp_j) {                                          /* a new row of cells: the random numbers of the cells of the three rows around it */
        int k, c;
        for (k = 0; k < 3; k++)
            for (c = 0; c < r->gp_ncx + 2; c++) r->gp_h[k][c] = grain_hash((unsigned)(c - 1), (unsigned)(j + k - 1), lk->gp_seed);
        r->gp_j = j;
    }
    gp_blend(ly, SE_GP_H, &nby, &wys, &wyo);
    gp_cells_row(r, lk, 0, j, ly);
    if (wyo) gp_cells_row(r, lk, 1, j + nby, ly);
    for (x = 0; x < w; x++) {
        int t[3];
        uint8_t *p = row + x * 3;
        gp_samp(r, 0, &r->gp_c[x], t);
        if (wyo) {
            int t2[3];
            gp_samp(r, 1, &r->gp_c[x], t2);
            t[0] = (wys * t[0] + wyo * t2[0]) >> 8;
            t[1] = (wys * t[1] + wyo * t2[1]) >> 8;
            t[2] = (wys * t[2] + wyo * t2[2]) >> 8;
        }
        p[0] = (uint8_t)clamp8(p[0] + ((t[0] * T[p[0]] + 32768) >> 16));
        p[1] = (uint8_t)clamp8(p[1] + ((t[1] * T[p[1]] + 32768) >> 16));
        p[2] = (uint8_t)clamp8(p[2] + ((t[2] * T[p[2]] + 32768) >> 16));
    }
}

/* Darkened corners: the factor depends on the squared distance from the centre of the frame. */
static void vignette_row(const se_look *lk, const se_look_run *r, uint8_t *row, int w, int y)
{
    int x, dy = 2 * y - (lk->vig_h - 1);
    int dy2 = (int)(((unsigned)(dy * dy)) >> r->vshift);
    for (x = 0; x < w; x++) {
        int idx = ((r->vx2[x] + dy2) * r->vscale) >> 16;
        int f;
        if (idx > 255) idx = 255;
        f = lk->vig_tab[idx];
        if (f != 256) {
            row[x * 3 + 0] = (uint8_t)((row[x * 3 + 0] * f) >> 8);
            row[x * 3 + 1] = (uint8_t)((row[x * 3 + 1] * f) >> 8);
            row[x * 3 + 2] = (uint8_t)((row[x * 3 + 2] * f) >> 8);
        }
    }
}

/* One row, fully graded and already in `dst`, ready to go out. */
static se_err emit_one(se_look_run *r, uint8_t *dst,
                       se_look_emit emit, void *ctx)
{
    if (r->vx2) vignette_row(&r->lk, r, dst, r->w, r->y_out);
    if (r->lk.grain > 0) {
        if (r->gp_c) grain_row_plate(&r->lk, r, dst, r->w, r->y_out);
        else grain_row(&r->lk, r, dst, r->w, r->y_out);
    }
    r->y_out++;
    return emit(ctx, dst, r->w) == 0 ? SE_OK : SE_E_IO;
}

/*
 * Push one source row into the delay line, and emit whatever row that
 * completes.
 *
 * THE INDEXING, because it is the one thing here that is easy to get wrong
 * and impossible to see wrong in a photograph:
 *
 *   The ring is primed with `r` extra copies of row 0, so source row j
 *   lands at push index j + r. After push index p, the ring holds push
 *   indices p-2r .. p, which is exactly the vertical window centred on
 *   output row p - 2r. That row's RGB is at push index p - r, still in the
 *   ring because nring = 2r+1 > r.
 *
 *   Top and bottom edges are handled by replication -- the priming copies
 *   at the top, and `r` more copies of the last mask row in se_look_end at
 *   the bottom. Clamping the window instead would need a per-row divisor
 *   and would still be a guess about what is off the edge of the frame.
 *
 *   Total pushes are H + 2r and emits start at p = 2r, so exactly H rows
 *   come out however short the image is -- including H < r, where nothing
 *   at all is emitted until the flush.
 */
static se_err emit_delayed(se_look_run *r, int p,
                           se_look_emit emit, void *ctx);

static se_err push_row(se_look_run *r, const uint8_t *src,
                       se_look_emit emit, void *ctx)
{
    int      w    = r->w;
    int      slot = r->pushed % r->nring;
    int      dslot = r->pushed % r->ndelay;
    uint8_t *dly  = r->delay + (size_t)dslot * w * 3;
    uint8_t *msk  = r->ring  + (size_t)slot * w;
    int      x, p;

    /* The row leaving the window stops contributing to the column sums.
     * Only once the ring has been round at least once, or we would be
     * subtracting the zeros se_look_begin put there. */
    if (r->pushed >= r->nring)
        for (x = 0; x < w; x++) r->colsum[x] -= msk[x];

    memcpy(dly, src, (size_t)w * 3);
    grade_row(&r->lk, r->lut, dly, w);
    mask_row(&r->lk, dly, w, r->scratch, msk);

    for (x = 0; x < w; x++) r->colsum[x] += msk[x];

    p = r->pushed++;
    if (p < 2 * r->r) return SE_OK;          /* window not centred yet */
    return emit_delayed(r, p, emit, ctx);
}

/* Emit the output row whose window has just closed at push index p. */
static se_err emit_delayed(se_look_run *r, int p,
                           se_look_emit emit, void *ctx)
{
    int      slot = (p - r->r) % r->ndelay;
    uint8_t *dly  = r->delay + (size_t)slot * r->w * 3;

    add_glow(&r->lk, dly, r->w, r->colsum, r->nring);
    return emit_one(r, dly, emit, ctx);
}

se_err se_look_rows(se_look_run *r, const uint8_t *rgb, int nrows,
                    se_look_emit emit, void *ctx)
{
    int    i;
    se_err e;

    if (!r || !rgb || nrows < 0 || !emit) return SE_E_CORRUPT;

    for (i = 0; i < nrows; i++) {
        const uint8_t *src = rgb + (size_t)i * r->w * 3;

        if (!r->on) {
            /* No halation, so no neighbourhood and no delay: grade and go.
             * The caller's rows are const and may be the decoder's own
             * scratch, so the grade lands in our row, never in theirs. */
            memcpy(r->flat, src, (size_t)r->w * 3);
            grade_row(&r->lk, r->lut, r->flat, r->w);
            e = emit_one(r, r->flat, emit, ctx);
            if (e != SE_OK) return e;
            continue;
        }

        /* First row of the frame: prime the top edge with r copies of it,
         * so the window is full from the first emit onwards. */
        if (r->pushed == 0) {
            int k;
            for (k = 0; k < r->r; k++) {
                e = push_row(r, src, emit, ctx);
                if (e != SE_OK) return e;
            }
        }

        e = push_row(r, src, emit, ctx);
        if (e != SE_OK) return e;
    }
    return SE_OK;
}

se_err se_look_end(se_look_run *r, se_look_emit emit, void *ctx)
{
    int    k, w, last;
    se_err e;

    if (!r || !emit) return SE_E_CORRUPT;
    if (!r->on || r->pushed == 0) return SE_OK;

    w = r->w;

    /* The bottom edge, replicated: copy the last mask row down `r` times.
     * The mask is copied rather than re-derived because the graded RGB it
     * came from has since had glow and grain written into it -- masking
     * that again would feed the glow back through itself. */
    for (k = 0; k < r->r; k++) {
        int slot, x;
        last = (r->pushed - 1) % r->nring;
        slot = r->pushed % r->nring;

        if (r->pushed >= r->nring)
            for (x = 0; x < w; x++) r->colsum[x] -= r->ring[(size_t)slot * w + x];
        memcpy(r->ring + (size_t)slot * w,
                  r->ring + (size_t)last * w, (size_t)w);
        for (x = 0; x < w; x++) r->colsum[x] += r->ring[(size_t)slot * w + x];

        r->pushed++;
        if (r->pushed - 1 < 2 * r->r) continue;
        e = emit_delayed(r, r->pushed - 1, emit, ctx);
        if (e != SE_OK) return e;
    }
    return SE_OK;
}
