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
