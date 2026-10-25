/* lookbuild.h - turns a film (films.h) plus the panel sliders (look_t) into the parameters of the JPG engine
 * (engine/look.c). Integer arithmetic only (a module has no floating point), shared by the camera and the PC test.
 *
 * It starts from fl_collect() in xmp.h, the same numbers that go into the Lightroom sidecar, and renders them
 * the way Lightroom does, as far as a finished JPEG allows:
 *   exposure, contrast, highlights, shadows, whites, blacks and the tone curves  -> three 256 entry curves,
 *   HSL (8 bands) / B&W mixer, saturation, vibrance                              -> per-hue tables,
 *   colour grading (shadows / midtones / highlights / global)                    -> split toning,
 *   grain, vignette, halation                                                    -> the spatial stages.
 * White balance, clarity, texture, dehaze, sharpening and noise reduction of the presets are not applied to the JPG. */
#ifndef FL_LOOKBUILD_H
#define FL_LOOKBUILD_H

#include "xmp.h"
#include "engine/look.h"

/* sRGB code -> linear light, 0..65535 */
static const unsigned short lb_s2l[256] = {
    0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219,
    241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562,
    599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101,
    1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
    1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866,
    2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129,
    4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669,
    5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
    7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635,
    9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
    12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874,
    15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481,
    21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
    25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542,
    29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138,
    39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
    45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341,
    50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221,
    63795, 64372, 64952, 65535,
};
/* 2^(k/8) in 16.16 */
static const unsigned int lb_p2[9] = { 65536, 71468, 77936, 84990, 92682, 101070, 110218, 120194, 131072 };

static int lb_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* linear light (0..65535) -> sRGB, as 0..4096 */
static int lb_l2s(int lin)
{
    int lo = 0, hi = 255, a, b, fr;
    if (lin <= 0) return 0;
    if (lin >= 65535) return 4096;
    while (hi - lo > 1) { int m = (lo + hi) >> 1; if (lb_s2l[m] <= lin) lo = m; else hi = m; }
    a = lb_s2l[lo]; b = lb_s2l[hi];
    fr = b > a ? ((lin - a) * 4096) / (b - a) : 0;
    return (lo * 4096 + fr) / 255;
}

static int lb_smooth(int x)                      /* smoothstep, 0..4096 */
{
    int a = (x * x) >> 12;
    return (a * (3 * 4096 - 2 * x)) >> 12;
}

static int lb_bell(int x, int lo, int hi)        /* 4t(1-t) over [lo, hi], 0..4096 */
{
    int t;
    if (x <= lo || x >= hi) return 0;
    t = ((x - lo) << 12) / (hi - lo);
    return (4 * t * (4096 - t)) >> 12;
}

typedef struct { int exp, con, hi, sh, wh, bl; } lb_adj;

/* the global tone sliders of Lightroom, applied to an sRGB value in 0..4096 */
static int lb_tone(int i, const lb_adj *p)
{
    int y = (i * 4096 + 127) / 255;
    if (p->exp) {
        int e = p->exp < 0 ? -p->exp : p->exp;
        int whole = e / 100, rem = e % 100;
        unsigned int m, lin;
        int f8 = (rem * 8 * 256) / 100, k = f8 >> 8, fr = f8 & 255;
        if (whole > 5) whole = 5;
        m = lb_p2[k] + (((lb_p2[k + 1] - lb_p2[k]) * (unsigned)fr) >> 8);
        m <<= whole;
        lin = lb_s2l[i];
        if (p->exp > 0) {
            unsigned int knee = 45875u;
            lin = (unsigned int)(((unsigned long long)lin * m) >> 16);
            if (lin > 262000u) lin = 262000u;
            if (lin > knee) { unsigned int t = lin - knee, r = 65535u - knee; lin = knee + (r * t) / (t + r); }
        } else {
            lin = (lin << 16) / m;                      /* lin < 65536, so this stays within 32 bits (no 64-bit divide) */
        }
        y = lb_l2s((int)lin);
    }
    if (p->con) y += (p->con * (lb_smooth(y) - y)) / 100;
    if (p->hi) y += ((p->hi * lb_bell(y, 1434, 4096)) / 100 * 614) >> 12;
    if (p->sh) y += ((p->sh * lb_bell(y, 0, 2662)) / 100 * 614) >> 12;
    if (p->wh && y > 2458) { int t = ((y - 2458) << 12) / (4096 - 2458); y += ((p->wh * ((t * t) >> 12)) / 100 * 491) >> 12; }
    if (p->bl && y < 1638) { int t = ((1638 - y) << 12) / 1638; y += ((p->bl * ((t * t) >> 12)) / 100 * 491) >> 12; }
    return lb_clamp(y, 0, 4096);
}

/* a tone curve of the preset (points 0..255), monotone cubic Hermite, blended with the straight line by Efekt */
typedef struct { int n; int x[CURVE_MAX], y[CURVE_MAX], d[CURVE_MAX]; } lb_crv;

static void lb_crv_init(lb_crv *c, const film_t *f, int k, int strength)
{
    int n = f->nc[k], j, straight = 1;
    int m[CURVE_MAX];
    c->n = 0;
    if (n < 2) return;
    for (j = 0; j < n; j++) {
        int x = f->cv[k][j][0], y = f->cv[k][j][1];
        c->x[j] = x;
        c->y[j] = x + ((y - x) * strength) / 100;
        if (c->y[j] != x) straight = 0;
    }
    if (straight) return;
    for (j = 0; j + 1 < n; j++) {
        int dx = c->x[j + 1] - c->x[j];
        m[j] = dx > 0 ? lb_clamp(((c->y[j + 1] - c->y[j]) * 4096) / dx, -16384, 16384) : 0;
    }
    c->d[0] = m[0]; c->d[n - 1] = m[n - 2];
    for (j = 1; j + 1 < n; j++)
        c->d[j] = (m[j - 1] * m[j] <= 0) ? 0 : (2 * m[j - 1] * m[j]) / (m[j - 1] + m[j]);
    c->n = n;
}

/* v: 0..4096, result 0..4096 */
static int lb_crv_eval(const lb_crv *c, int v)
{
    int X = v * 255, j, t, t2, t3, h00, h10, h01, h11, h, yq;
    if (X <= c->x[0] * 4096) return (c->y[0] * 4096) / 255;
    if (X >= c->x[c->n - 1] * 4096) return (c->y[c->n - 1] * 4096) / 255;
    for (j = 0; j + 2 < c->n && X >= c->x[j + 1] * 4096; j++) {}
    h = c->x[j + 1] - c->x[j];
    if (h <= 0) return (c->y[j] * 4096) / 255;
    t = (X - c->x[j] * 4096) / h;                          /* 0..4096 */
    t2 = (t * t) >> 12; t3 = (t2 * t) >> 12;
    h00 = 2 * t3 - 3 * t2 + 4096; h10 = t3 - 2 * t2 + t; h01 = -2 * t3 + 3 * t2; h11 = t3 - t2;
    yq = h00 * c->y[j] + h01 * c->y[j + 1]
       + (((h10 * c->d[j]) >> 12) * h) + (((h11 * c->d[j + 1]) >> 12) * h);        /* 0..255 in 12 bit fixed point */
    return lb_clamp(yq / 255, 0, 4096);
}

/* the 8 Lightroom colour bands on the 1536 step colour wheel: R O Y G A B P M */
static const short lb_band_c[9] = { 0, 128, 256, 512, 768, 1024, 1152, 1280, 1536 };
static const char *const lb_band_name[8] = { "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta" };

static void lb_hue_table(int16_t *dst, const int *band)
{
    int k = 0, h;
    for (h = 0; h < 1536; h++) {
        int c0, c1, v0, v1;
        while (h >= lb_band_c[k + 1]) k++;
        c0 = lb_band_c[k]; c1 = lb_band_c[k + 1];
        v0 = band[k]; v1 = band[(k + 1) & 7];
        dst[h] = (int16_t)(v0 + ((v1 - v0) * (h - c0)) / (c1 - c0));
    }
}

static int lb_val(const int *val, const char *a, const char *b)       /* value of attribute a + b, 0 if absent */
{
    char name[48]; int i = 0, id;
    while (*a && i < 40) name[i++] = *a++;
    while (*b && i < 47) name[i++] = *b++;
    name[i] = 0;
    id = xa_id(name);
    return id >= 0 ? val[id] : 0;
}

/* colour of a wheel position (hue in degrees, saturation 0..100) as a signed offset that leaves the brightness alone;
 * scaled by k (percent), added to out[3] with weight w (percent) */
static void lb_tint_add(int *out, int hue, int sat, int k, int w)
{
    int h = ((hue % 360) + 360) % 360, i = h / 60, f = ((h % 60) * 255) / 60, r, g, b, y, c;
    switch (i) {
    case 0:  r = 255;     g = f;       b = 0;       break;
    case 1:  r = 255 - f; g = 255;     b = 0;       break;
    case 2:  r = 0;       g = 255;     b = f;       break;
    case 3:  r = 0;       g = 255 - f; b = 255;     break;
    case 4:  r = f;       g = 0;       b = 255;     break;
    default: r = 255;     g = 0;       b = 255 - f; break;
    }
    y = (77 * r + 150 * g + 29 * b) >> 8;
    c = r - y; out[0] += (c * sat * k / 100 / 100) * w / 100;
    c = g - y; out[1] += (c * sat * k / 100 / 100) * w / 100;
    c = b - y; out[2] += (c * sat * k / 100 / 100) * w / 100;
}

static int lb_has(const char *s, const char *needle)
{
    int i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; j++) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

/* halation that the film has by itself (stocks without the remjet layer) */
static int lb_film_halation(const film_t *f)
{
    if (lb_has(f->full, "Cinestill") || lb_has(f->full, "800T") || lb_has(f->full, "Phoenix")) return 45;
    if (lb_has(f->full, "Redscale")) return 25;
    return 0;
}

/* arena: 9 KB for the per-hue tables. scale: decode scale (1, 2, 4) of the delivered frame, out_h its height.
 * Returns 0, or -1 when the arena is too small. */
static int fl_build_look(se_look *lk, se_arena *ar, const film_t *f, const look_t *L, int scale, int out_h)
{
    int val[ATTR_COUNT];
    unsigned char has[ATTR_COUNT];
    int strength = lb_clamp(L->strength, 0, 100);
    int mono = f->mono && strength > 0;
    lb_adj ad;
    lb_crv cv[4];
    int i, c, lift, liftq, sat, vib, hal;

    memset(lk, 0, sizeof *lk);
    fl_collect(val, has, f, L);

    /* tone: one curve for the three channels, then the channel curves */
    ad.exp = lb_val(val, "Exposure2012", ""); ad.con = lb_val(val, "Contrast2012", "");
    ad.hi = lb_val(val, "Highlights2012", ""); ad.sh = lb_val(val, "Shadows2012", "");
    ad.wh = lb_val(val, "Whites2012", ""); ad.bl = lb_val(val, "Blacks2012", "");
    for (c = 0; c < 4; c++) lb_crv_init(&cv[c], f, c, strength);
    lift = lb_clamp(L->fade, 0, 100) * 60 / 100;                  /* Matowość: lifted blacks, 0..60 of 255 */
    liftq = (lift * 4096) / 255;
    for (i = 0; i < 256; i++) {
        int y = lb_tone(i, &ad);
        if (cv[0].n) y = lb_crv_eval(&cv[0], y);
        if (lift) y = liftq + (y * (255 - lift)) / 255;
        for (c = 0; c < 3; c++) {
            int yc = cv[c + 1].n ? lb_crv_eval(&cv[c + 1], y) : y;
            lk->map[c][i] = (uint8_t)lb_clamp((yc * 255 + 2048) >> 12, 0, 255);
        }
    }

    /* saturation and vibrance (vibrance counted as half a saturation step) */
    sat = lb_val(val, "Saturation", ""); vib = lb_val(val, "Vibrance", "");
    lk->sat = lb_clamp((256 * (100 + sat) * (200 + vib)) / 20000, 0, 1024);
    if (mono) lk->sat = 0;

    /* per hue: saturation / rotation / brightness from the HSL panel, or the B&W mixer for black and white films */
    {
        int bs[8], bl[8], br[8], any = 0;
        for (i = 0; i < 8; i++) {
            if (mono) {
                int g = lb_val(val, "GrayMixer", lb_band_name[i]);
                bs[i] = 256; br[i] = 0; bl[i] = lb_clamp(256 + (g * 205) / 100, 0, 1024);
            } else {
                int s = lb_val(val, "SaturationAdjustment", lb_band_name[i]);
                int h = lb_val(val, "HueAdjustment", lb_band_name[i]);
                int l = lb_val(val, "LuminanceAdjustment", lb_band_name[i]);
                bs[i] = lb_clamp((256 * (100 + s)) / 100, 0, 1024);
                br[i] = (h * 128) / 100;
                bl[i] = lb_clamp(256 + (l * 154) / 100, 0, 1024);
            }
            if (bs[i] != 256 || bl[i] != 256 || br[i] != 0) any = 1;
        }
        if (any) {
            int16_t *t = SE_NEW(ar, int16_t, 3 * 1536);
            if (!t) return -1;
            lb_hue_table(t, bs); lb_hue_table(t + 1536, bl); lb_hue_table(t + 3072, br);
            lk->hs = t; lk->hl = t + 1536; lk->hr = t + 3072; lk->hue_any = 1;
        }
    }

    /* colour grading -> split toning (shadows weigh dark pixels, highlights bright ones; midtones and global both) */
    {
        int sh = lb_val(val, "ColorGradeShadowSat", ""), hs_ = lb_val(val, "ColorGradeHighlightSat", "");
        int ms = lb_val(val, "ColorGradeMidtoneSat", ""), gs = lb_val(val, "ColorGradeGlobalSat", "");
        /* on a colour picture the tint is added to colours that are already there; on a black and white one it is the
         * only colour, and Lightroom shows it much stronger (a sepia needs a grading saturation of only 15..20) */
        int k = mono ? 60 : 22;
        if (sh > 0) lb_tint_add(lk->sh, lb_val(val, "ColorGradeShadowHue", ""), sh, k, 100);
        if (hs_ > 0) lb_tint_add(lk->hi, lb_val(val, "ColorGradeHighlightHue", ""), hs_, k, 100);
        if (ms > 0) {
            lb_tint_add(lk->sh, lb_val(val, "ColorGradeMidtoneHue", ""), ms, k, 80);
            lb_tint_add(lk->hi, lb_val(val, "ColorGradeMidtoneHue", ""), ms, k, 80);
        }
        if (gs > 0) {
            lb_tint_add(lk->sh, lb_val(val, "ColorGradeGlobalHue", ""), gs, k, 100);
            lb_tint_add(lk->hi, lb_val(val, "ColorGradeGlobalHue", ""), gs, k, 100);
        }
    }

    /* grain */
    lk->grain = lb_clamp(lb_val(val, "GrainAmount", ""), 0, 100);
    lk->grain_cell = 1 + lb_val(val, "GrainSize", "") / 12;
    if (scale > 1) lk->grain_cell = (lk->grain_cell + scale - 1) / scale;
    if (lk->grain_cell < 1) lk->grain_cell = 1;

    /* vignette: Lightroom's PostCropVignetteAmount is negative for dark corners */
    {
        int dark = lb_clamp(-lb_val(val, "PostCropVignetteAmount", ""), 0, 100);
        if (dark > 0) {
            lk->vig_amount = dark; lk->vig_h = out_h;
            for (i = 0; i < 256; i++) {
                int q = 0;
                if (i > 64) { int r = ((i - 64) * 256) / 191; q = (r * r) >> 8; }          /* 0..256 */
                lk->vig_tab[i] = (uint8_t)lb_clamp(256 - (dark * 217 * q) / (100 * 256), 0, 255);
            }
            lk->vig_tab[0] = 255;
        }
    }

    /* halation: the film's own (Cinestill 800T ...) plus the slider */
    hal = lb_clamp(lb_film_halation(f) + L->hal, 0, 100);
    if (hal > 0) {
        lk->hal_amount = hal * 3;
        lk->hal_thresh = 180;
        lk->hal_radius = lb_clamp(28 / (scale > 0 ? scale : 1), 1, 48);
        lk->hal_tint[0] = 255; lk->hal_tint[1] = 80; lk->hal_tint[2] = 36;
    }
    return 0;
}

#endif
