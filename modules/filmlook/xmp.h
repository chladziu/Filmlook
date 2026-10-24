/* xmp.h - builds the Lightroom / Camera Raw sidecar. No snprintf: only %d-free manual formatting,
 * so it behaves the same in the camera and on a PC (see test_xmp.c). */
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
    if (neg) xb_s(b, "-"); else if (with_sign) xb_s(b, "+");
    while (k > 0) { char c[2]; c[0] = t[--k]; c[1] = 0; xb_s(b, c); }
}

static int xl_clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int xl_scale(int v, int strength) { return (v * strength) / 100; }

static void xb_attr(xb_t *b, const char *name, int v, int sign)
{
    xb_s(b, "   "); xb_s(b, name); xb_s(b, "=\""); xb_i(b, v, sign); xb_s(b, "\"\n");
}

/* strength 0..100, grain_pct 0..200 (100 = as in table), con_add -50..50. Returns length or -1. */
static int build_xmp(char *buf, int cap, const film_t *f, int strength, int grain_pct, int con_add)
{
    xb_t b; int i;
    b.buf = buf; b.cap = cap; b.n = 0; b.err = 0; buf[0] = 0;
    strength = xl_clamp(strength, 0, 100);

    xb_s(&b, "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");
    xb_s(&b, "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n");
    xb_s(&b, " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n");
    xb_s(&b, "  <rdf:Description rdf:about=\"\"\n");
    xb_s(&b, "   xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\"\n");
    xb_s(&b, "   xmlns:film=\"http://ns.example.com/filmlook/1.0/\"\n");
    xb_s(&b, "   xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n");
    xb_s(&b, "   crs:Version=\"15.0\"\n   crs:ProcessVersion=\"11.0\"\n   crs:HasSettings=\"True\"\n");
    xb_s(&b, "   film:Name=\""); xb_s(&b, f->name); xb_s(&b, "\"\n");
    xb_attr(&b, "film:Strength", strength, 0);
    xb_s(&b, f->mono ? "   crs:ConvertToGrayscale=\"True\"\n" : "   crs:ConvertToGrayscale=\"False\"\n");
    xb_attr(&b, "crs:Contrast2012",   xl_clamp(xl_scale(f->con, strength) + con_add, -100, 100), 1);
    xb_attr(&b, "crs:Highlights2012", xl_scale(f->hi, strength), 1);
    xb_attr(&b, "crs:Shadows2012",    xl_scale(f->sh, strength), 1);
    xb_attr(&b, "crs:Whites2012",     xl_scale(f->wh, strength), 1);
    xb_attr(&b, "crs:Blacks2012",     xl_scale(f->bl, strength), 1);
    if (!f->mono) {
        xb_attr(&b, "crs:Saturation", xl_scale(f->sat, strength), 1);
        xb_attr(&b, "crs:Vibrance",   xl_scale(f->vib, strength), 1);
        xb_attr(&b, "crs:SplitToningShadowHue", xl_clamp(f->sh_h, 0, 359), 0);
        xb_attr(&b, "crs:SplitToningShadowSaturation", xl_scale(f->sh_s, strength), 0);
        xb_attr(&b, "crs:SplitToningHighlightHue", xl_clamp(f->hi_h, 0, 359), 0);
        xb_attr(&b, "crs:SplitToningHighlightSaturation", xl_scale(f->hi_s, strength), 0);
        xb_attr(&b, "crs:SplitToningBalance", 0, 0);
    }
    xb_attr(&b, "crs:GrainAmount", xl_clamp((xl_scale(f->g, strength) * grain_pct) / 100, 0, 100), 0);
    xb_attr(&b, "crs:GrainSize", xl_clamp(f->gs, 0, 100), 0);
    xb_attr(&b, "crs:GrainFrequency", xl_clamp(f->gr, 0, 100), 0);
    xb_s(&b, "   crs:ToneCurveName2012=\"Custom\">\n");
    xb_s(&b, "   <crs:ToneCurvePV2012>\n    <rdf:Seq>\n");
    for (i = 0; i < 5; i++) {
        int x = f->curve[i][0], y = f->curve[i][1];
        int o = x + ((y - x) * strength) / 100;
        xb_s(&b, "     <rdf:li>"); xb_i(&b, xl_clamp(x, 0, 255), 0);
        xb_s(&b, ", "); xb_i(&b, xl_clamp(o, 0, 255), 0); xb_s(&b, "</rdf:li>\n");
    }
    xb_s(&b, "    </rdf:Seq>\n   </crs:ToneCurvePV2012>\n");
    xb_s(&b, "   <dc:subject>\n    <rdf:Bag>\n     <rdf:li>Film: "); xb_s(&b, f->name);
    xb_s(&b, "</rdf:li>\n    </rdf:Bag>\n   </dc:subject>\n");
    xb_s(&b, "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>\n");
    return b.err ? -1 : b.n;
}

#endif
