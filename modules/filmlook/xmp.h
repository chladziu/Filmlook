/* xmp.h - builds the Lightroom / Camera Raw sidecar from a film (films.h). No snprintf: manual number formatting,
 * so it behaves the same in the camera and on a PC (see test_xmp.c).
 *
 * The preset is reproduced 1:1 at Efekt = 100 %. Below that:
 *   - all tone / colour sliders (also HSL, B&W mixer, calibration) are scaled linearly towards 0,
 *   - the tone curves are blended towards the straight line,
 *   - sharpening / noise reduction / white balance of the preset are written as they are (only when Efekt > 0),
 *   - Efekt = 0: the file only carries the film name and the Kontrast / Ziarno changes, no look and no B&W conversion.
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

/* strength 0..100, grain_pct 0..200 (100 = as in the preset), con_add -50..50. Returns length or -1. */
static int build_xmp(char *buf, int cap, const film_t *f, int strength, int grain_pct, int con_add)
{
    static const char *const curve_tag[4] = { "ToneCurvePV2012", "ToneCurvePV2012Red",
                                              "ToneCurvePV2012Green", "ToneCurvePV2012Blue" };
    xb_t b; int i, c, custom = 0;
    b.buf = buf; b.cap = cap; b.n = 0; b.err = 0; buf[0] = 0;
    strength = xl_clamp(strength, 0, 100);

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

    /* all numeric values of the preset */
    for (i = 0; i < f->nv; i++) {
        const attr_t *a = &attr_defs[f->v[i].id];
        int v = f->v[i].v;
        if ((a->flags & F_TECH) && strength == 0) continue;
        if (a->flags & F_SCALE) v = xl_scale(v, strength);
        if (a->flags & F_CON) v = xl_clamp(v + con_add, -100, 100);
        if (a->flags & F_GRAIN) v = xl_clamp((v * grain_pct) / 100, 0, 100);
        if (a->flags & F_HUND) xb_attr_hund(&b, a->name, v);
        else if (a->flags & F_TENTHS) xb_attr_tenths(&b, a->name, v);
        else xb_attr(&b, a->name, v, (a->flags & F_SIGN) ? 1 : 0);
    }

    for (c = 0; c < 4; c++) if (!curve_is_straight(f, c)) custom = 1;
    xb_attr_s(&b, "ToneCurveName2012", custom ? "Custom" : "Linear");
    xb_s(&b, ">\n");

    for (c = 0; c < 4; c++) {
        xb_s(&b, "   <crs:"); xb_s(&b, curve_tag[c]); xb_s(&b, ">\n    <rdf:Seq>\n");
        for (i = 0; i < f->nc[c]; i++) {
            int x = f->cv[c][i][0], y = f->cv[c][i][1];
            int o = x + ((y - x) * strength) / 100;
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
