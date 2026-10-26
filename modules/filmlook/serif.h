/*
 * serif.h - prints the serif letters of serif_data.h (made by tools/make_serif_font.py) into the 8-bit screen buffer of
 * the camera, and draws the rounded bars.
 *
 * Every letter is a small picture with 4 levels (0 = paper ... 3 = full ink). A text line is composed in a row buffer
 * first and written to the screen once, row after row, over the whole rectangle of the line: the old text of that
 * rectangle is replaced in one pass, so there is no blinking and nothing has to be wiped before.
 * Integer arithmetic only (the module has no floating point).
 *
 * The caller sets ui_fb / ui_pitch (the camera: bmp_vram() and BMPPITCH) and fills ui_ramp[colour][level] with
 * palette indices; see panel.h.
 */
#ifndef SERIF_H
#define SERIF_H

typedef struct { unsigned short code, adv, w; unsigned char h; signed char x, y; unsigned int off; unsigned short kc; } sf_glyph;
typedef struct { const sf_glyph *g; int n; const unsigned char *bits; int asc, desc, size, track16; } sf_font;

#include "serif_data.h"

#define SF_LEFT   0
#define SF_RIGHT  1
#define SF_CENTER 2
#define SF_JUST   3

#define SF_RPAD   3                      /* right-aligned / justified text ends this many px before the end of its rectangle */
#define SF_MAXG   128
#define SF_MAXW   704

static unsigned char *ui_fb;                     /* the screen buffer, one byte per pixel (palette index) */
static int ui_pitch;                             /* bytes per row */
static unsigned char ui_ramp[8][4];              /* colour -> palette index of the 4 levels (0 = paper, 3 = full colour) */

static int sf_utf8(const char **ps)
{
    const unsigned char *p = (const unsigned char *)*ps;
    int c = p[0];
    if (c < 0x80) { *ps += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { *ps += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *ps += 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    *ps += 1;
    return '?';
}

static const sf_glyph *sf_find(const sf_font *f, int code)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (f->g[m].code == code) return &f->g[m];
        if (f->g[m].code < code) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

static int sf_kern(const sf_font *f, int a, int b)           /* 1/16 px */
{
    unsigned int key = ((unsigned int)a << 16) | (unsigned int)b;
    int lo = 0, hi = SFD_KERN_N - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (sfd_kkey[m] == key) return ((int)sfd_kval[m] * f->size * 16) / 1000;
        if (sfd_kkey[m] < key) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

/* laid out line: any number of runs (font + colour + text), one after the other */
typedef struct { const sf_glyph *g; int x16; int gaps; const sf_font *f; unsigned char col; } sf_pos;
typedef struct { const sf_font *f; const char *s; int col; } sf_run;
static sf_pos sf_buf[SF_MAXG];
static int sf_n;                                 /* letters in sf_buf */
static int sf_ngaps;                             /* gaps that may stretch (justified text) */
static int sf_lx, sf_lprev, sf_lgp, sf_ltrk;     /* state of the layout */

static void sf_lay_begin(void)
{
    sf_n = 0; sf_lx = 0; sf_lprev = 0; sf_lgp = 0; sf_ltrk = 0;
}

/* appends s (up to n bytes, -1 = all) in the font f and colour col. Gaps that may stretch (justified text):
 * the tabs if the text has any, otherwise the spaces */
static void sf_lay_run(const sf_font *f, const char *s, int n, int col)
{
    int has_tab = 0;
    const char *e = (n < 0) ? (const char *)0 : s + n;
    const char *p;
    for (p = s; e ? p < e : *p; p++) if (*p == '\t') { has_tab = 1; break; }
    sf_lprev = 0;                                /* no kerning between two runs */
    while (e ? s < e : *s) {
        int c = sf_utf8(&s);
        const sf_glyph *g;
        if (sf_n >= SF_MAXG) break;
        g = sf_find(f, c == '\t' ? ' ' : c);
        if (!g) g = sf_find(f, '?');
        if (!g) continue;
        if (sf_lprev) sf_lx += sf_kern(f, sf_lprev, g->kc);
        sf_buf[sf_n].g = g; sf_buf[sf_n].x16 = sf_lx; sf_buf[sf_n].gaps = sf_lgp;
        sf_buf[sf_n].f = f; sf_buf[sf_n].col = (unsigned char)col;
        if (has_tab ? c == '\t' : c == ' ') sf_lgp++;
        sf_lx += g->adv + f->track16;
        sf_ltrk = f->track16;
        sf_lprev = g->kc;
        sf_n++;
    }
}

/* returns the width in 1/16 px */
static int sf_lay_end(void)
{
    sf_ngaps = sf_lgp;
    return sf_lx - (sf_n ? sf_ltrk : 0);
}

static int sf_layout(const sf_font *f, const char *s, int n)
{
    sf_lay_begin();
    sf_lay_run(f, s, n, 0);
    return sf_lay_end();
}

/* width of a text in px */
static int sf_width(const sf_font *f, const char *s)
{
    return (sf_layout(f, s, -1) + 8) >> 4;
}

/* width of several runs in px */
static int sf_runs_width(const sf_run *r, int nr)
{
    int i;
    sf_lay_begin();
    for (i = 0; i < nr; i++) sf_lay_run(r[i].f, r[i].s, -1, r[i].col);
    return (sf_lay_end() + 8) >> 4;
}

static unsigned char sf_lvl[SF_MAXW];             /* per pixel: level 0..3 + 4 * colour */

/* writes the laid out line: the rectangle x0..x1 (exclusive) x [ybase - top, ybase + bot) gets the text, aligned left /
 * right / centre / justified; everything else of the rectangle becomes paper. Ink outside the rows is cut off. */
static void sf_emit(int x0, int x1, int ybase, int top, int bot, int tw16, int align)
{
    int W = x1 - x0, r, i;
    int shift16 = 0, extra16 = 0, rem = 0;
    int rows = top + bot;
    int Wt;                                       /* width the text is aligned in: the rectangle is SF_RPAD wider on the right,
                                                   * so that the last letter (an overhanging j, y ...) is not cut off */
    if (W <= 0 || W > SF_MAXW) return;
    Wt = (align == SF_RIGHT || align == SF_JUST) ? W - SF_RPAD : W;
    if (align == SF_RIGHT) shift16 = Wt * 16 - tw16;
    else if (align == SF_CENTER) shift16 = (W * 16 - tw16) / 2;
    else if (align == SF_JUST && sf_ngaps > 0 && tw16 < Wt * 16) { extra16 = (Wt * 16 - tw16) / sf_ngaps; rem = (Wt * 16 - tw16) - extra16 * sf_ngaps; }
    if (shift16 < 0) shift16 = 0;
    for (r = 0; r < rows; r++) {
        int yy = ybase - top + r;
        unsigned char *row = ui_fb + yy * ui_pitch + x0;
        memset(sf_lvl, 0, (size_t)W);
        for (i = 0; i < sf_n; i++) {
            const sf_glyph *g = sf_buf[i].g;
            int gy = r - top - g->y;
            if (gy >= 0 && gy < g->h) {
                int stride = (g->w + 3) >> 2, k;
                const unsigned char *p = sf_buf[i].f->bits + g->off + gy * stride;
                int gx = ((sf_buf[i].x16 + shift16 + extra16 * sf_buf[i].gaps + (sf_buf[i].gaps < rem ? sf_buf[i].gaps : rem) + 8) >> 4) + g->x;
                int cc = sf_buf[i].col << 2;
                for (k = 0; k < g->w; k++) {
                    int v = (p[k >> 2] >> (6 - 2 * (k & 3))) & 3;
                    int px = gx + k;
                    if (v && px >= 0 && px < W && v > (sf_lvl[px] & 3)) sf_lvl[px] = (unsigned char)(v | cc);
                }
            }
        }
        for (i = 0; i < W; i++) row[i] = ui_ramp[sf_lvl[i] >> 2][sf_lvl[i] & 3];
    }
}

static void sf_line_n(const sf_font *f, int x0, int x1, int ybase, int top, int bot, const char *s, int n, int col, int align)
{
    int tw16;
    sf_lay_begin();
    sf_lay_run(f, s, n, col);
    tw16 = sf_lay_end();
    sf_emit(x0, x1, ybase, top, bot, tw16, align);
}

/* several runs (font, text, colour) in one line, written in one pass */
static void sf_line_runs(int x0, int x1, int ybase, int top, int bot, const sf_run *r, int nr, int align)
{
    int i, tw16;
    sf_lay_begin();
    for (i = 0; i < nr; i++) sf_lay_run(r[i].f, r[i].s, -1, r[i].col);
    tw16 = sf_lay_end();
    sf_emit(x0, x1, ybase, top, bot, tw16, align);
}

static void sf_line(const sf_font *f, int x0, int x1, int ybase, int top, int bot, const char *s, int col, int align)
{
    sf_line_n(f, x0, x1, ybase, top, bot, s, -1, col, align);
}

/* a paragraph: greedy wrap into lines of at most W px; all lines but the last are justified when they are nearly full.
 * Returns the number of lines drawn (at most max_lines, the rest of the text is cut). Lines not used are NOT cleared. */
static int sf_para(const sf_font *f, int x0, int x1, int ybase, int top, int bot, int pitch, int max_lines, const char *s, int col, int centre)
{
    int W16 = (x1 - x0 - SF_RPAD) * 16, nl = 0;
    while (*s && nl < max_lines) {
        const char *ls = s, *best = 0, *p = s;
        int bw = 0;
        for (;;) {                                    /* p runs over the word ends */
            const char *q = p;
            while (*q && *q != ' ') q++;
            if (sf_layout(f, ls, (int)(q - ls)) > W16 && best) break;
            best = q; bw = sf_layout(f, ls, (int)(q - ls));
            if (!*q) break;
            p = q + 1;
        }
        if (!best) best = s + strlen(s);
        {
            int last = (*best == 0);
            int just = (!last && bw * 10 >= W16 * 7) || (last && bw * 100 >= W16 * 90);   /* the last line only when it is nearly full */
            sf_line_n(f, x0, x1, ybase + nl * pitch, top, bot, ls, (int)(best - ls), col, centre ? SF_CENTER : (just ? SF_JUST : SF_LEFT));
            s = *best == ' ' ? best + 1 : best;
        }
        nl++;
    }
    return nl;
}

/* ------------------------------------------------------------------ plain shapes */
static void ui_fill(int col, int x, int y, int w, int h)
{
    int r;
    unsigned char c = ui_ramp[col][3];
    for (r = 0; r < h; r++) memset(ui_fb + (y + r) * ui_pitch + x, c, (size_t)w);
}

static int ui_isqrt(int n)
{
    int r = 0, b = 1 << 20;
    while (b > n) b >>= 2;
    while (b) {
        if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* coverage level 0..3 of pixel (px, py) in the capsule [a, b) x [y0, y0 + h), the ends rounded when rl / rr are set */
static int ui_cap(int px, int py, int a, int b, int y0, int h, int rl, int rr)
{
    int r = h / 2, cx = -1, dx, dy, d, v;
    if (px < a || px >= b || py < y0 || py >= y0 + h) return 0;
    if (rl && px < a + r) cx = a + r;
    else if (rr && px >= b - r) cx = b - r;
    if (cx < 0) return 3;
    dx = 4 * px + 2 - 4 * cx;
    dy = 4 * py + 2 - 4 * (y0 + r);
    d = ui_isqrt(dx * dx + dy * dy);
    v = ((4 * r - d + 2) * 3 + 2);
    if (v <= 0) return 0;
    v /= 4;
    return v > 3 ? 3 : v;
}

/* Bar with rounded ends: the track x..x+w (colour track), the part fx0..fx1 filled with colour fill.
 * ra / rb: the filled part is rounded at its left / right end (not at the zero line of a two-sided bar).
 * mark >= 0: a small marker (2 x 4 px, colour soft) above the bar at that x (the zero / normal position).
 * mx0..mx1 (when mx1 > mx0): a second part in colour mcol, rounded at its ends as mra / mrb say. With mtop it lies on top
 * of the filled part (the added part of a changed slider), without it behind the filled part (the part taken away).
 * The whole band of the row (y - 11 .. y + h + 8, columns xa .. xb-1) is written in this one pass: whatever stood there
 * before (the marker or the bar of another row after the list scrolled) is replaced, nothing is wiped first. */
static void ui_bar(int x, int y, int w, int h, int fx0, int fx1, int fill, int track, int ra, int rb, int mark, int xa, int xb, int soft,
                   int mx0, int mx1, int mcol, int mra, int mrb, int mtop)
{
    int px, py;
    unsigned char bg = ui_ramp[track][0];
    int same = (mx0 == fx0 && mx1 == fx1);             /* the changed part is the whole filled part */
    if (fx1 > fx0 && fx1 - fx0 < h) {                  /* a very small value is still a visible dot */
        if (rb) fx1 = fx0 + h; else fx0 = fx1 - h;
        if (fx0 < x) { fx0 = x; fx1 = x + h; }
        if (fx1 > x + w) { fx1 = x + w; fx0 = x + w - h; }
    }
    if (same) { mx0 = fx0; mx1 = fx1; }
    if (mx1 > mx0) {
        if (mx1 - mx0 < 3) mx1 = mx0 + 3;
        if (mx1 - mx0 < h) { mra = 0; mrb = 0; }       /* a short part is cut square: round ends would not fit */
    }
    for (py = y - 11; py < y + h + 9; py++) {
        unsigned char *row = ui_fb + py * ui_pitch;
        int in_bar = (py >= y && py < y + h);
        int in_mark = (mark >= 0 && py >= y - 5 && py < y - 1);
        for (px = xa; px < xb; px++) {
            if (in_bar) {
                int lt = ui_cap(px, py, x, x + w, y, h, 1, 1);
                int lf = (fx1 > fx0) ? ui_cap(px, py, fx0, fx1, y, h, ra, rb) : 0;
                int lm = (mx1 > mx0) ? ui_cap(px, py, mx0, mx1, y, h, mra, mrb) : 0;
                if (lm > 0 && mtop)  row[px] = ui_ramp[mcol][lm];
                else if (lf > 0)     row[px] = ui_ramp[fill][lf];
                else if (lm > 0)     row[px] = ui_ramp[mcol][lm >= 3 ? 2 : 1];     /* what was taken away: a lighter tone */
                else                 row[px] = ui_ramp[track][lt];
            } else if (in_mark && px >= mark - 1 && px < mark + 1) row[px] = ui_ramp[soft][3];
            else row[px] = bg;
        }
    }
}

#endif
