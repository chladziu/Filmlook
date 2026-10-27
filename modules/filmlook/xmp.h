/* xmp.h - builds the Lightroom / Camera Raw sidecar from a film (films.h). No snprintf: manual number formatting,
 * so it behaves the same in the camera and on a PC (see test_xmp.c).
 *
 * The preset is reproduced 1:1 at Efekt = 100 % (and all look sliders at 0). Below that:
 *   - all tone / colour sliders (also HSL, B&W mixer, calibration) are scaled linearly towards 0,
 *   - the tone curves are blended towards the straight line,
 *   - sharpening / noise reduction / white balance of the preset are written as they are (only when Efekt > 0),
 *   - Efekt = 0: the file only carries the film name, the Kontrast / Ziarno changes and the look sliders, no film look and
 *     no B&W conversion.
 * The look sliders (Matowość, Ciepło, Cienie, Światła, Nasycenie, Winieta) are applied after the scaling, so they work the
 * same on every film and on the empty "Własny look". The curve of the preset is never replaced, only lifted / blended.
 * Black & white films always get crs:ConvertToGrayscale="True" (the attribute Lightroom's own B&W presets use). The
 * <crs:Look> block and crs:Treatment of the EmulsionPresets files are deliberately NOT written: their identifiers are
 * not verified and Lightroom may ignore the whole preset (the B&W conversion included) when they do not match. */
#ifndef FL_XMP_H
#define FL_XMP_H

#include "films.h"

typedef struct { char *buf; int cap; int n; int err; } xb_t;

static void xb_s(xb_t *b, const char *s)
{
    while (*s) {
        if (b->n + 1 >= b->cap) { b->err = 1; return; }
        b->buf[b->n++] = *s++;
    }
    b->buf[b->n] = 0;
}

static void xb_i(xb_t *b, int v, int with_sign)
{
    char t[16]; int k = 0; int neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-v) : (unsigned int)v;
    if (u == 0) t[k++] = '0';
    while (u > 0) { t[k++] = '0' + (u % 10); u /= 10; }
    if (neg) xb_s(b, "-"); else if (with_sign && v > 0) xb_s(b, "+");
    while (k > 0) { char c[2]; c[0] = t[--k]; c[1] = 0; xb_s(b, c); }
}

static int xl_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int xl_scale(int v, int strength) { return (v * strength) / 100; }

static void xb_attr(xb_t *b, const char *name, int v, int sign)
{
    xb_s(b, "   crs:"); xb_s(b, name); xb_s(b, "=\""); xb_i(b, v, sign); xb_s(b, "\"\n");
}

static void xb_attr_s(xb_t *b, const char *name, const char *v)
{
    xb_s(b, "   crs:"); xb_s(b, name); xb_s(b, "=\""); xb_s(b, v); xb_s(b, "\"\n");
}

static void xb_attr_tenths(xb_t *b, const char *name, int v)     /* "+1.5" */
{
    xb_s(b, "   crs:"); xb_s(b, name); xb_s(b, "=\"+"); xb_i(b, v / 10, 0); xb_s(b, ".");
    xb_i(b, v % 10, 0); xb_s(b, "\"\n");
}

static void xb_attr_hund(xb_t *b, const char *name, int v)       /* "+0.30" / "-0.15" / "0.00" */
{
    int a = v < 0 ? -v : v;
    xb_s(b, "   crs:"); xb_s(b, name); xb_s(b, "=\"");
    if (v < 0) xb_s(b, "-"); else if (v > 0) xb_s(b, "+");
    xb_i(b, a / 100, 0); xb_s(b, "."); xb_i(b, (a % 100) / 10, 0); xb_i(b, a % 10, 0);
    xb_s(b, "\"\n");
}

static int curve_is_straight(const film_t *f, int c)
{
    int i;
    for (i = 0; i < f->nc[c]; i++) if (f->cv[c][i][0] != f->cv[c][i][1]) return 0;
    return 1;
}

#define UC_MAX 12   /* points of one user curve (look_t.uc_*) */

/* ---- the "own look" layer: sliders that sit on top of any film (or on the empty "Własny look") ---- */
typedef struct {
    int strength;   /* Efekt     0..100   how much of the film preset is used */
    int grain;      /* Ziarno    0..200   % of the grain of the preset */
    int contrast;   /* Kontrast  -50..50 */
    int fade;       /* Matowość  0..100   lifts the blacks (milky shadows) */
    int warm;       /* Ciepło    -100..100  cool .. warm cast over the whole picture */
    int shadow;     /* Cienie    -100..100  shadows: teal/blue .. amber */
    int high;       /* Światła   -100..100  highlights: blue .. amber */
    int sat;        /* Nasycenie -100..100 */
    int vig;        /* Winieta   0..100   dark corners */
    int hal;        /* Halation  0..100   glow round bright lights (JPG only) */
    /* the desktop program (desktop/) adds more sliders on top of the camera's nine. All of them are 0 when untouched, so the
     * camera module (which never sets them) and every picture made without them stay exactly as they were. */
    int expo;       /* Ekspozycja     -300..300  hundredths of a stop, added to the preset's exposure */
    int hi_t;       /* Jasne partie   -100..100  highlights (tone), added to the preset's */
    int sh_t;       /* Ciemne partie  -100..100  shadows (tone) */
    int wh_t;       /* Biele          -100..100  whites */
    int bl_t;       /* Czernie        -100..100  blacks */
    int gsize;      /* Wielkość ziarna  -50..50  added to the preset's grain size */
    int grough;     /* Szorstkość ziarna -50..50 added to the preset's grain roughness */
    int ton_mode;   /* 0 the film's own colour grading stays, 1 it is cleared first (the wheels below are then all there is) */
    int cg[8];      /* colour grading wheels (the "split toning" of Lightroom): shadows, midtones, highlights, global, each as
                     * hue 0..359 degrees and saturation 0..100 */
    int hsl[24];    /* colour mixer: hue[8], saturation[8], luminance[8] of red, orange, yellow, green, aqua, blue, purple, magenta, each -100..100
                     * (on black and white films only the luminance counts: it is the black and white mixer) */
    int filter;     /* colour filter in front of the lens (black and white films only): 0 none, 1 yellow, 2 orange, 3 red, 4 green, 5 blue */
    int filter_amt; /* strength of the filter 0..100 (100 = full); only used when filter > 0 */
    int wb_temp;    /* Temperatura     -100..100  blue .. amber, applied to the picture in linear light (JPG engine only) */
    int wb_tint;    /* Odcień          -100..100  green .. magenta (JPG engine only) */
    int skin_hue;   /* Tony skóry: odcień     -100..100  redder .. yellower (the orange band, with a little of red and yellow) */
    int skin_sat;   /* Tony skóry: nasycenie  -100..100 */
    int skin_lum;   /* Tony skóry: jasność    -100..100  (on black and white films: how bright skin comes out in grey) */
    int uc_n[4];    /* Krzywe: points of the curve the user draws (master, red, green, blue); 0 or 1 = no curve. Applied after the film's own curves */
    int uc_x[4][UC_MAX], uc_y[4][UC_MAX];    /* the points: input 0..255, output 0..255, input growing */
    int gcolor;     /* Kolor ziarna   0..100  colour films only: how much of the grain is coloured (every channel grains on its own,
                     * like the dye clouds of the three layers of a colour film). 0 = the grain is grey, as it always was in the camera */
    int usejpg;     /* desktop: 1 = work from the JPEG inside the file even when the file holds the sensor data (RAW); 0 = RAW when possible */
    int rawref;     /* desktop, RAW: 1 = the tone of the camera's own JPEG is the starting point ("wzorzec z JPG"), 0 = the plain RAW */
    int hq;         /* desktop: 1 = best quality (the JPEG with full resolution colours, 4:4:4, and every pixel graded exactly), 0 = the camera's way */
    /* desktop: the crop and the size of the result (zero in the camera). The crop is a rectangle of the picture as it is shown (the camera's
     * orientation applied), in millionths of its width and height; empty (x1 <= x0 or y1 <= y0) = the whole picture. crop_ar_w : crop_ar_h
     * fits the crop to that ratio exactly (the biggest one that fits, in the middle of the rectangle); crop_ar_auto: the ratio is
     * long side : short side, whichever way the rectangle lies. The size: out_long = the long side in pixels, or out_w / out_h =
     * the width / the height (the other side follows); all 0 = the crop as it is. */
    int crop_x0, crop_y0, crop_x1, crop_y1;
    int crop_ar_w, crop_ar_h, crop_ar_auto;
    int out_long, out_w, out_h;
    /* desktop: straightening, like the angle of the crop tool of Lightroom. crop_rot = the angle in thousandths of a degree
     * (-90000..90000; positive turns the picture clockwise); the crop (above) is then a rectangle of the TURNED picture and has to
     * lie inside it (it is shrunk when it does not; without a crop the biggest rectangle of the shape of the picture is taken).
     * turn = quarter turns clockwise of the picture as it is shown (0..3), done by the orientation tag of the result: no pixel moves. */
    int crop_rot, turn;
} look_t;

static int xa_id(const char *name)
{
    int i;
    for (i = 0; i < ATTR_COUNT; i++) {
        const char *a = attr_defs[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

/* cos in 1/1000 at 5 degree steps, linear in between: accurate to about 0.3 %, plenty for a colour cast */
static int xcos(int d)
{
    static const short c5[19] = { 1000, 996, 985, 966, 940, 906, 866, 819, 766, 707, 643, 574, 500, 423, 342, 259, 174, 87, 0 };
    int neg = 0, i, r;
    d %= 360; if (d < 0) d += 360;
    if (d > 180) d = 360 - d;
    if (d > 90) { d = 180 - d; neg = 1; }
    i = d / 5; r = d % 5;
    { int v = (i >= 18) ? 0 : c5[i] + ((c5[i + 1] - c5[i]) * r) / 5; return neg ? -v : v; }
}
static int xsin(int d) { return xcos(d - 90); }

static int xisqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* adds delta to an attribute of the preset (0 when the preset has none), within [lo, hi] */
static void xl_add_attr(int *val, unsigned char *has, const char *name, int delta, int lo, int hi)
{
    int id;
    if (delta == 0) return;
    id = xa_id(name);
    if (id < 0) return;
    val[id] = xl_clamp((has[id] ? val[id] : 0) + delta, lo, hi); has[id] = 1;
}

/* adds a colour cast (hue in degrees, amount = saturation) to a Colour Grading wheel; if the preset already
 * uses that wheel the two casts are added as vectors, so neither is lost */
static void xtint_add(int *val, unsigned char *has, const char *hue_name, const char *sat_name, int hue, int amount)
{
    int hi = xa_id(hue_name), si = xa_id(sat_name);
    int x = 0, y = 0, best = 0, bestd = 0, a, mag;
    if (hi < 0 || si < 0 || amount <= 0) return;
    if (has[si] && val[si] > 0) {
        int h0 = has[hi] ? val[hi] : 0;
        x = val[si] * xcos(h0); y = val[si] * xsin(h0);
    }
    x += amount * xcos(hue); y += amount * xsin(hue);
    for (a = 0; a < 360; a++) {
        int dot = (x / 16) * xcos(a) + (y / 16) * xsin(a);
        if (dot > bestd) { bestd = dot; best = a; }
    }
    mag = xisqrt((x / 100) * (x / 100) + (y / 100) * (y / 100));       /* sat * 10 */
    val[hi] = best; has[hi] = 1;
    val[si] = xl_clamp((mag + 5) / 10, 0, 100); has[si] = 1;
}

/* the attributes of the colour mixer by band; kind 0 hue, 1 saturation, 2 luminance, 3 black and white mixer. Static
 * tables, because a module has no snprintf. */
static const char *const xl_band_attr[4][8] = {
    { "HueAdjustmentRed", "HueAdjustmentOrange", "HueAdjustmentYellow", "HueAdjustmentGreen",
      "HueAdjustmentAqua", "HueAdjustmentBlue", "HueAdjustmentPurple", "HueAdjustmentMagenta" },
    { "SaturationAdjustmentRed", "SaturationAdjustmentOrange", "SaturationAdjustmentYellow", "SaturationAdjustmentGreen",
      "SaturationAdjustmentAqua", "SaturationAdjustmentBlue", "SaturationAdjustmentPurple", "SaturationAdjustmentMagenta" },
    { "LuminanceAdjustmentRed", "LuminanceAdjustmentOrange", "LuminanceAdjustmentYellow", "LuminanceAdjustmentGreen",
      "LuminanceAdjustmentAqua", "LuminanceAdjustmentBlue", "LuminanceAdjustmentPurple", "LuminanceAdjustmentMagenta" },
    { "GrayMixerRed", "GrayMixerOrange", "GrayMixerYellow", "GrayMixerGreen",
      "GrayMixerAqua", "GrayMixerBlue", "GrayMixerPurple", "GrayMixerMagenta" }
};

/* The numbers of the preset after Efekt, Ziarno, Kontrast and the look sliders: val[id] / has[id] for every attribute.
 * Shared by the Lightroom sidecar (build_xmp) and the JPG engine (lookbuild.h), so both start from the same values. */
static void fl_collect(int *val, unsigned char *has, const film_t *f, const look_t *L)
{
    int i;
    int strength = xl_clamp(L->strength, 0, 100);
    for (i = 0; i < ATTR_COUNT; i++) { val[i] = 0; has[i] = 0; }
    /* 1. numeric values of the preset, scaled by Efekt, with the Kontrast / Ziarno sliders */
    for (i = 0; i < f->nv; i++) {
        int id = f->v[i].id;
        const attr_t *a = &attr_defs[id];
        int v = f->v[i].v;
        if ((a->flags & F_TECH) && strength == 0) continue;
        if (a->flags & F_SCALE) v = xl_scale(v, strength);
        if (a->flags & F_CON) v = xl_clamp(v + L->contrast, -100, 100);
        if (a->flags & F_GRAIN) v = xl_clamp((v * L->grain) / 100, 0, 100);
        val[id] = v; has[id] = 1;
    }
    /* Kontrast also works when the film carries no contrast value (e.g. "Własny look") */
    if (L->contrast != 0) {
        int id = xa_id("Contrast2012");
        if (id >= 0 && !has[id]) { val[id] = xl_clamp(L->contrast, -100, 100); has[id] = 1; }
    }

    /* 2. the look sliders, on top of everything above */
    if (L->sat != 0) {
        int id = xa_id("Saturation");
        if (id >= 0) { val[id] = xl_clamp((has[id] ? val[id] : 0) + L->sat, -100, 100); has[id] = 1; }
    }
    if (L->vig > 0) {
        int id = xa_id("PostCropVignetteAmount");
        if (id >= 0) { val[id] = xl_clamp((has[id] ? val[id] : 0) - L->vig, -100, 100); has[id] = 1; }
    }
    /* 2b. the desktop's own sliders (all zero in the camera) */
    xl_add_attr(val, has, "Exposure2012", L->expo, -500, 500);
    xl_add_attr(val, has, "Highlights2012", L->hi_t, -100, 100);
    xl_add_attr(val, has, "Shadows2012", L->sh_t, -100, 100);
    xl_add_attr(val, has, "Whites2012", L->wh_t, -100, 100);
    xl_add_attr(val, has, "Blacks2012", L->bl_t, -100, 100);
    xl_add_attr(val, has, "GrainSize", L->gsize, 0, 100);
    xl_add_attr(val, has, "GrainFrequency", L->grough, 0, 100);
    /* colour mixer (8 bands x hue / saturation / luminance); on black and white films the luminance goes to the black and white mixer */
    {
        int k, bw = (f->mono && strength > 0);
        for (k = 0; k < 8; k++) {
            if (bw) {
                xl_add_attr(val, has, xl_band_attr[3][k], L->hsl[16 + k], -100, 100);
            } else {
                xl_add_attr(val, has, xl_band_attr[0][k], L->hsl[k], -100, 100);
                xl_add_attr(val, has, xl_band_attr[1][k], L->hsl[8 + k], -100, 100);
                xl_add_attr(val, has, xl_band_attr[2][k], L->hsl[16 + k], -100, 100);
            }
        }
    }
    /* colour grading: the preset's own grading is cleared when asked, then the wheels are added */
    {
        static const char *const hn[4] = { "ColorGradeShadowHue", "ColorGradeMidtoneHue", "ColorGradeHighlightHue", "ColorGradeGlobalHue" };
        static const char *const sn[4] = { "ColorGradeShadowSat", "ColorGradeMidtoneSat", "ColorGradeHighlightSat", "ColorGradeGlobalSat" };
        int k, id;
        if (L->ton_mode == 1) for (k = 0; k < 4; k++) { id = xa_id(sn[k]); if (id >= 0) { val[id] = 0; has[id] = 0; } }
        for (k = 0; k < 4; k++) {
            int sa = xl_clamp(L->cg[2 * k + 1], 0, 100);
            if (sa > 0) xtint_add(val, has, hn[k], sn[k], ((L->cg[2 * k] % 360) + 360) % 360, sa);
        }
    }
    if (L->skin_hue || L->skin_sat || L->skin_lum) {
        /* skin lives in the orange band of the colour wheel; red and yellow take part with less weight so that the change
         * has no edge (band numbers: 0 red, 1 orange, 2 yellow; weights in percent) */
        static const int band[3] = { 1, 0, 2 };
        static const int wt[3] = { 100, 50, 30 };
        int k, bw = (f->mono && strength > 0);
        for (k = 0; k < 3; k++) {
            if (!bw) {
                xl_add_attr(val, has, xl_band_attr[0][band[k]], (L->skin_hue * wt[k]) / 300, -100, 100);   /* skin tolerates only a third of the hue range */
                xl_add_attr(val, has, xl_band_attr[1][band[k]], (L->skin_sat * wt[k]) / 100, -100, 100);
                xl_add_attr(val, has, xl_band_attr[2][band[k]], (L->skin_lum * wt[k]) / 100, -100, 100);
            } else {
                xl_add_attr(val, has, xl_band_attr[3][band[k]], (L->skin_lum * wt[k]) / 100, -100, 100);
            }
        }
    }
    if (f->mono && strength > 0) {
        /* a colour filter acts through the black and white mixer: how bright each colour comes out in the grey picture
         * (rows: yellow, orange, red, green, blue; columns: R O Y G A B P M) */
        static const signed char flt[5][8] = {
            {  10,  25,  40,   0, -15, -35, -20,   0 },
            {  30,  45,  25, -10, -30, -50, -30,   5 },
            {  60,  60,  30, -30, -55, -70, -45,  20 },
            { -25,  -5,  25,  50,  25, -30, -25, -25 },
            { -50, -45, -30, -15,  20,  55,  40,  15 }
        };
        if (L->filter >= 1 && L->filter <= 5) {
            int k, amt = xl_clamp(L->filter_amt, 0, 100);
            for (k = 0; k < 8; k++) xl_add_attr(val, has, xl_band_attr[3][k], (flt[L->filter - 1][k] * amt) / 100, -100, 100);
        }
    }
    if (L->warm != 0)                                    /* amber .. blue over the whole picture */
        xtint_add(val, has, "ColorGradeGlobalHue", "ColorGradeGlobalSat", L->warm > 0 ? 38 : 218,
                  ((L->warm < 0 ? -L->warm : L->warm) * 25) / 100);
    if (L->shadow != 0)                                  /* amber .. teal in the shadows */
        xtint_add(val, has, "ColorGradeShadowHue", "ColorGradeShadowSat", L->shadow > 0 ? 35 : 195,
                  ((L->shadow < 0 ? -L->shadow : L->shadow) * 40) / 100);
    if (L->high != 0)                                    /* amber .. blue in the highlights */
        xtint_add(val, has, "ColorGradeHighlightHue", "ColorGradeHighlightSat", L->high > 0 ? 45 : 215,
                  ((L->high < 0 ? -L->high : L->high) * 40) / 100);

}

/* Returns length or -1. f = the film, L = Efekt / Ziarno / Kontrast and the look sliders. */
static int build_xmp(char *buf, int cap, const film_t *f, const look_t *L)
{
    static const char *const curve_tag[4] = { "ToneCurvePV2012", "ToneCurvePV2012Red",
                                              "ToneCurvePV2012Green", "ToneCurvePV2012Blue" };
    int val[ATTR_COUNT];
    unsigned char has[ATTR_COUNT];
    xb_t b; int i, c, custom = 0, lift;
    int strength = xl_clamp(L->strength, 0, 100);
    b.buf = buf; b.cap = cap; b.n = 0; b.err = 0; buf[0] = 0;

    xb_s(&b, "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
    xb_s(&b, "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n");
    xb_s(&b, " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n");
    xb_s(&b, "  <rdf:Description rdf:about=\"\"\n");
    xb_s(&b, "   xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\"\n");
    xb_s(&b, "   xmlns:film=\"http://ns.example.com/filmlook/1.0/\"\n");
    xb_s(&b, "   xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n");
    xb_s(&b, "   crs:Version=\"15.4\"\n   crs:ProcessVersion=\"15.4\"\n   crs:HasSettings=\"True\"\n");
    xb_s(&b, "   film:Name=\""); xb_s(&b, f->full); xb_s(&b, "\"\n");
    xb_s(&b, "   film:Strength=\""); xb_i(&b, strength, 0); xb_s(&b, "\"\n");

    /* look: black & white conversion, white balance, camera profile */
    xb_attr_s(&b, "ConvertToGrayscale", (f->mono && strength > 0) ? "True" : "False");
    if (strength > 0) {
        if (f->wb_custom) {
            xb_attr_s(&b, "WhiteBalance", "Custom");
            xb_attr(&b, "Temperature", f->temp, 0);
            xb_attr(&b, "Tint", f->tint, 1);
        }
        if (f->profile == 1) xb_attr_s(&b, "CameraProfile", "Adobe Standard");
        else if (f->profile == 2) xb_attr_s(&b, "CameraProfile", "Camera Standard");
    }

    fl_collect(val, has, f, L);

    for (i = 0; i < ATTR_COUNT; i++) {
        const attr_t *a = &attr_defs[i];
        if (!has[i]) continue;
        if (a->flags & F_HUND) xb_attr_hund(&b, a->name, val[i]);
        else if (a->flags & F_TENTHS) xb_attr_tenths(&b, a->name, val[i]);
        else xb_attr(&b, a->name, val[i], (a->flags & F_SIGN) ? 1 : 0);
    }

    /* 3. tone curves: preset curve blended with the straight line by Efekt, Matowość lifts the master curve */
    lift = xl_clamp(L->fade, 0, 100) * 60 / 100;
    for (c = 0; c < 4; c++) if (!curve_is_straight(f, c)) custom = 1;
    if (lift > 0) custom = 1;
    xb_attr_s(&b, "ToneCurveName2012", custom ? "Custom" : "Linear");
    xb_s(&b, ">\n");

    for (c = 0; c < 4; c++) {
        xb_s(&b, "   <crs:"); xb_s(&b, curve_tag[c]); xb_s(&b, ">\n    <rdf:Seq>\n");
        for (i = 0; i < f->nc[c]; i++) {
            int x = f->cv[c][i][0], y = f->cv[c][i][1];
            int o = x + ((y - x) * strength) / 100;
            if (c == 0 && lift > 0) o = lift + (o * (255 - lift)) / 255;
            xb_s(&b, "     <rdf:li>"); xb_i(&b, xl_clamp(x, 0, 255), 0);
            xb_s(&b, ", "); xb_i(&b, xl_clamp(o, 0, 255), 0); xb_s(&b, "</rdf:li>\n");
        }
        xb_s(&b, "    </rdf:Seq>\n   </crs:"); xb_s(&b, curve_tag[c]); xb_s(&b, ">\n");
    }
    xb_s(&b, "   <dc:subject>\n    <rdf:Bag>\n     <rdf:li>Film: "); xb_s(&b, f->full);
    xb_s(&b, "</rdf:li>\n    </rdf:Bag>\n   </dc:subject>\n");
    xb_s(&b, "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n");
    return b.err ? -1 : b.n;
}

#endif
