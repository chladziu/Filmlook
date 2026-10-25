/*
 * panel.h - how the panel looks: colours, positions and the drawing of every row. The camera (filmlook.c) and the PC
 * test of the screen (tools_host/uimock.c) use this same file, so the picture made on the PC is what the camera draws.
 *
 * Layout (720 x 480, margins 40 and 680, everything aligned to them):
 *   title (heavy retro serif)                                          version
 *   ---------------------------------------------------------------------
 *   type tabs: Bez efektu | C-41 | BW | Slajd | Własny      (spread over the full width)
 *   Marka                                                  ‹ Kodak ›
 *   ‹                       FILM NAME (large)                       ›
 *   kind of film · ISO                                             3 / 34
 *   description (justified)
 *   ---------------------------------------------------------------------
 *   5 visible rows of the list: label | rounded bar | value in %
 *   ---------------------------------------------------------------------
 *   key help (spread over the full width)
 * All text is the serif face of serif_data.h, 4 levels of grey, painted into 16 palette colours of the camera.
 */
#ifndef PANEL_H
#define PANEL_H

#include "serif.h"

/* ------------------------------------------------------------------ colours */
enum { PC_BG, PC_TEXT, PC_ACCENT, PC_SOFT, PC_DIM, PC_TRACK, PC_N };
static const unsigned char pc_rgb[PC_N][3] = {
    { 229, 222, 202 },        /* PC_BG     paper  #E5DECA */
    {  38,  33,  28 },        /* PC_TEXT   ink    #26211C */
    { 160,  62,  24 },        /* PC_ACCENT rust   #A03E18 */
    {  96,  88,  74 },        /* PC_SOFT   #60584A */
    { 150, 141, 120 },        /* PC_DIM    lines, switched-off rows #968D78 */
    { 207, 198, 174 },        /* PC_TRACK  empty part of a bar */
};
#define PAL_BASE   232                         /* first palette index used (the camera gets them back when the panel closes) */
#define PAL_COUNT  (PC_N + 2 * (PC_N - 1))     /* 6 colours + 2 half tones of each of the 5 inks = 16 */

static unsigned char pal_rgb[PAL_COUNT][3];    /* the colour of palette index PAL_BASE + i */

static void pal_build(void)
{
    int c, k, j;
    for (c = 0; c < PC_N; c++) for (j = 0; j < 3; j++) pal_rgb[c][j] = pc_rgb[c][j];
    for (c = 1; c < PC_N; c++)
        for (k = 1; k <= 2; k++)
            for (j = 0; j < 3; j++)
                pal_rgb[PC_N + 2 * (c - 1) + (k - 1)][j] = (unsigned char)(pc_rgb[0][j] + ((pc_rgb[c][j] - pc_rgb[0][j]) * k) / 3);
    for (c = 0; c < PC_N; c++) {
        ui_ramp[c][0] = PAL_BASE + PC_BG;
        ui_ramp[c][3] = PAL_BASE + c;
        ui_ramp[c][1] = c ? PAL_BASE + PC_N + 2 * (c - 1) : PAL_BASE + PC_BG;
        ui_ramp[c][2] = c ? PAL_BASE + PC_N + 2 * (c - 1) + 1 : PAL_BASE + PC_BG;
    }
}

/* ------------------------------------------------------------------ geometry */
#define M_L        40                  /* left margin of the content */
#define M_R        680                 /* right margin */
#define M_R3      (M_R + SF_RPAD)      /* right end of the rectangle of a right-aligned / justified text */
#define MARK_X    24                  /* the selection mark of a row */
#define MARK_W    4

#define Y_TITLE   38                  /* baselines */
#define Y_RULE_A  48
#define Y_TABS    78
#define Y_BRAND   112
#define Y_NAME    172
#define Y_KIND    210
#define Y_DESC    241
#define Y_RULE_B  255
#define SL_Y0     260                 /* top of the list */
#define SL_PITCH  35
#define Y_RULE_C  440
#define Y_HINT    465

#define BAR_X     210
#define BAR_W     330
#define BAR_H     12
#define VAL_X     560

static const char *const tab_label[] = { "Bez efektu", "C-41", "BW", "Slajd", "Własny" };

static void draw_rule(int y) { ui_fill(PC_DIM, M_L, y, M_R - M_L, 1); }

static void draw_hint(const char *msg)
{
    if (msg) sf_line(&sf_xxs, M_L, M_R, Y_HINT, 17, 5, msg, PC_ACCENT, SF_CENTER);
    else     sf_line(&sf_xxs, M_L, M_R3, Y_HINT, 17, 5, "pokrętło: zmiana\tjoystick: wiersz\tSET: zapis\tkosz: zamknij", PC_SOFT, SF_JUST);
}

static void draw_static(void)
{
    int r;
    for (r = 0; r < 480; r++) memset(ui_fb + r * ui_pitch, ui_ramp[PC_BG][3], 720);
    sf_line(&sf_title, M_L, M_L + 480, Y_TITLE, 28, 3, "\001", PC_ACCENT, SF_LEFT);
    sf_line(&sf_xxs, M_R - 90, M_R3, Y_TITLE, 17, 5, VERSION, PC_SOFT, SF_RIGHT);
    draw_rule(Y_RULE_A);
    draw_rule(Y_RULE_B);
    draw_rule(Y_RULE_C);
    draw_hint(0);
}

/* the mark at the left of the row: rust when the row is selected */
static void draw_mark(int on, int y, int h)
{
    ui_fill(on ? PC_ACCENT : PC_BG, MARK_X, y, MARK_W, h);
}

/* row 0: the film type, a group in the middle of the screen. Every tab has a fixed slot (as wide as its bold text plus
 * the gap), so nothing moves when the choice changes, and the slots plus a blank at both ends cover the whole row:
 * every pixel is rewritten in one pass, no wiping before. The chosen tab is bold (and rust while the row is selected). */
#define TAB_GAP 24
static const char *tab_text(int i) { return i < 5 ? tab_label[i] : types[i - 1].name; }

static void tab_layout(int *x, int *w)           /* x[i], w[i]: slot of tab i (x[n] = end of the last slot) */
{
    int i, n = TYPE_COUNT + 1, tot = 0, s0;
    for (i = 0; i < n; i++) { w[i] = sf_width(&sf_xsb, tab_text(i)) + TAB_GAP; tot += w[i]; }
    s0 = (M_L + M_R) / 2 - tot / 2;
    for (i = 0; i < n; i++) { x[i] = s0; s0 += w[i]; }
    x[n] = s0;
}

static void draw_tabs(void)
{
    int on = (row == 0), ct = cur_type(), i, n = TYPE_COUNT + 1;
    int x[9], w[8];
    tab_layout(x, w);
    draw_mark(on, Y_TABS - 22, 28);
    sf_line(&sf_xs, M_L, x[0], Y_TABS, 24, 7, " ", PC_BG, SF_LEFT);                  /* blank before and after the group */
    sf_line(&sf_xs, x[n], M_R3, Y_TABS, 24, 7, " ", PC_BG, SF_LEFT);
    for (i = 0; i < n; i++) {
        int sel = (i == ct);
        sf_line(sel ? &sf_xsb : &sf_xs, x[i], x[i] + w[i], Y_TABS, 24, 7, tab_text(i), sel ? (on ? PC_ACCENT : PC_TEXT) : PC_SOFT, SF_CENTER);
    }
}

/* row 1: brand ("Marka  ‹ Kodak ›", centred, the chosen brand bold),
 * row 2: the film name, large, the arrows right next to it */
static void draw_sel(int r)
{
    int on = (row == r);
    int off = (st_film == 0);

    if (r == 1) {
        sf_run run[3];
        const char *val = off ? "-" : groups[films[st_film - 1].group].brand;
        char tmp[64];
        int c = off ? PC_DIM : (on ? PC_ACCENT : PC_TEXT);
        draw_mark(on, Y_BRAND - 22, 28);
        run[0].f = &sf_xs; run[0].s = "Marka     "; run[0].col = off ? PC_DIM : PC_SOFT;
        if (off) snprintf(tmp, sizeof(tmp), "%s", val);
        else     snprintf(tmp, sizeof(tmp), "\xE2\x80\xB9   %s   \xE2\x80\xBA", val);
        run[1].f = off ? &sf_xs : &sf_xsb; run[1].s = tmp; run[1].col = c;
        sf_line_runs(M_L, M_R3, Y_BRAND, 24, 7, run, 2, SF_CENTER);
    } else {
        sf_run run[3];
        const char *name = off ? "Bez efektu" : films[st_film - 1].name;
        const sf_font *f = &sf_big;
        int c = off ? PC_DIM : (on ? PC_ACCENT : PC_TEXT);
        int ca = off ? PC_BG : (on ? PC_ACCENT : PC_DIM);
        if (sf_width(f, name) > M_R - M_L - 100) f = &sf_mid;
        draw_mark(on, Y_NAME - 40, 48);
        run[0].f = &sf_big; run[0].s = off ? " " : "\xE2\x80\xB9 "; run[0].col = ca;
        run[1].f = f;       run[1].s = name;                      run[1].col = c;
        run[2].f = &sf_big; run[2].s = off ? " " : " \xE2\x80\xBA"; run[2].col = ca;
        sf_line_runs(M_L, M_R3, Y_NAME, 50, 14, run, 3, SF_CENTER);
    }
}

/* kind of film · ISO · number in the brand, then the description: both lines centred */
static void draw_info(void)
{
    char tmp[96], num[24];
    if (st_film > 0) {
        const film_t *f = &films[st_film - 1];
        const group_t *g = &groups[f->group];
        const char *d = films[eff_film() - 1].desc;
        const sf_font *kf = &sf_xs, *df = &sf_xs;
        sf_run run[2];
        if (f->iso > 0) snprintf(tmp, sizeof(tmp), "%s \xC2\xB7 ISO %d", f->kind, f->iso);
        else            snprintf(tmp, sizeof(tmp), "%s", f->kind);
        snprintf(num, sizeof(num), "   \xC2\xB7   %d / %d", st_film - g->first, g->n);
        run[0].f = kf; run[0].s = tmp; run[0].col = PC_ACCENT;
        run[1].f = kf; run[1].s = num; run[1].col = PC_SOFT;
        if (sf_runs_width(run, 2) > M_R - M_L) { kf = &sf_xxs; run[0].f = kf; run[1].f = kf; }   /* very long kinds use the smallest size */
        sf_line_runs(M_L, M_R3, Y_KIND, 24, 7, run, 2, SF_CENTER);
        if (sf_width(df, d) > M_R - M_L) df = &sf_xxs;                          /* very long descriptions use the smallest size */
        sf_para(df, M_L, M_R3, Y_DESC, 24, 7, 30, 1, d, PC_TEXT, 1);
    } else {
        sf_line(&sf_xs, M_L, M_R3, Y_KIND, 24, 7, "Efekt filmu wyłączony", PC_ACCENT, SF_CENTER);
        sf_line(&sf_xs, M_L, M_R3, Y_DESC, 24, 7, "Zdjęcia zostają bez zmian, plik XMP nie powstaje", PC_TEXT, SF_CENTER);
    }
}

/* ------------------------------------------------------------------ list of 5 rows */
static int slot_y(int r) { return SL_Y0 + (lst_pos(r) - top) * SL_PITCH + 27; }          /* baseline of row r */

static void list_row_frame(int r, int on, const char *label, int lcol)
{
    int yb = slot_y(r);
    draw_mark(on, yb - 22, 26);
    sf_line(&sf_sml, M_L, BAR_X - 12, yb, 25, 7, label, lcol, SF_LEFT);
}

/* x of a value on the bar */
static int bar_pos(int v, int lo, int hi) { return BAR_X + ((v - lo) * BAR_W) / (hi - lo); }

/* a slider row: bar and value in % */
static void draw_slider(int r)                  /* r = 4 .. 13 */
{
    int j = r - 4, on = (row == r);
    int v = *sl_var(j), lo = sl_def[j].lo, hi = sl_def[j].hi;
    int yb = slot_y(r), x0, x1, ra, rb, zx;
    char tmp[16];

    list_row_frame(r, on, sl_def[j].name, on ? PC_ACCENT : PC_TEXT);
    zx = bar_pos(0, lo, hi);
    if (lo < 0) {                               /* two-sided: from the zero to the value */
        int vx = bar_pos(v, lo, hi);
        if (v > 0)      { x0 = zx; x1 = vx; ra = 0; rb = 1; }
        else if (v < 0) { x0 = vx; x1 = zx; ra = 1; rb = 0; }
        else            { x0 = x1 = zx; ra = rb = 0; }
    } else {
        x0 = BAR_X; x1 = bar_pos(v, lo, hi); ra = rb = 1;
        if (v == lo) x1 = x0;
    }
    ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, x0, x1, on ? PC_ACCENT : PC_SOFT, PC_TRACK, ra, rb, lo < 0 ? zx : -1, BAR_X - 12, VAL_X, PC_SOFT);
    if (sl_def[j].signed_) snprintf(tmp, sizeof(tmp), "%s%d%%", v > 0 ? "+" : "", v);
    else                   snprintf(tmp, sizeof(tmp), "%d%%", v);
    sf_line(&sf_sml, VAL_X, M_R3, yb, 25, 7, tmp, on ? PC_ACCENT : PC_TEXT, SF_RIGHT);
}

/* row 3: push / pull. -2 .. +2 stops, a notch where the film has a variant, the knob jumps from notch to notch */
#define PUSH_RANGE 2
static void draw_push(void)
{
    int on = (row == 3), n = n_opts(), yb = slot_y(3);
    int c = (n > 1) ? (on ? PC_ACCENT : PC_TEXT) : PC_DIM;
    int cx = BAR_X + BAR_W / 2, x0 = cx, x1 = cx, ra = 0, rb = 0;
    const char *val = st_film > 0 ? "brak" : "-";
    char pushv[12];

    list_row_frame(3, on, "Push/Pull", c);
    if (n > 1) {
        int kx = BAR_X + ((st_push + PUSH_RANGE) * BAR_W) / (2 * PUSH_RANGE);
        if (kx > cx) { x0 = cx; x1 = kx; rb = 1; } else if (kx < cx) { x0 = kx; x1 = cx; ra = 1; }
        ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, x0, x1, on ? PC_ACCENT : PC_SOFT, PC_TRACK, ra, rb, cx, BAR_X - 12, VAL_X, PC_SOFT);   /* one marker: the normal position, like every other bar */
        if (st_push == 0) val = "normalnie";
        else { snprintf(pushv, sizeof(pushv), "%s%d EV", st_push > 0 ? "+" : "", st_push); val = pushv; }
    } else {
        ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, cx, cx, PC_TRACK, PC_TRACK, 0, 0, -1, BAR_X - 12, VAL_X, PC_SOFT);
    }
    sf_line(&sf_sml, VAL_X, M_R3, yb, 25, 7, val, c, SF_RIGHT);
}

/* last row: JPG with the film look, developed in the background after every photo */
static void draw_jpg(void)
{
    static const char *const opt[3] = { "wyłączony", "połowa (2808 px)", "pełny (5616 px)" };
    int on = (row == JROW), yb = slot_y(JROW);
    char tmp[48];
    list_row_frame(JROW, on, "JPG z efektem", on ? PC_ACCENT : PC_TEXT);
    snprintf(tmp, sizeof(tmp), "\xE2\x80\xB9  %s  \xE2\x80\xBA", opt[st_jpg]);
    sf_line(&sf_sml, BAR_X - 12, M_R3, yb, 25, 7, tmp, on ? PC_ACCENT : (st_jpg ? PC_TEXT : PC_SOFT), SF_RIGHT);
}

static void draw_slot(int r)
{
    int p = lst_pos(r);
    if (p < top || p >= top + VIS) return;           /* (also when the row does not exist for this film: p = -1) */
    if (r == 3) draw_push(); else if (r == JROW) draw_jpg(); else draw_slider(r);
}

/* a place of the list without a row: paper (and, without a film, a short note in the second place) */
static void draw_empty_slot(int i, const char *note)
{
    int yb = SL_Y0 + i * SL_PITCH + 27, y;
    for (y = yb - 25; y < yb + 7; y++) memset(ui_fb + y * ui_pitch + MARK_X, ui_ramp[PC_BG][3], (size_t)(M_R3 - MARK_X));
    if (note) sf_line(&sf_xs, M_L, M_R3, yb, 25, 7, note, PC_SOFT, SF_CENTER);
}

static void draw_scrollbar(void)
{
    const int x = 700, h = VIS * SL_PITCH, n = lst_n;
    int th = n > VIS ? (h * VIS) / n : h, ty = n > VIS ? (top * (h - th)) / (n - VIS) : 0, r;
    for (r = 0; r < h; r++)
        memset(ui_fb + (SL_Y0 + r) * ui_pitch + x, ui_ramp[n <= VIS ? PC_BG : ((r >= ty && r < ty + th) ? PC_SOFT : PC_TRACK)][3], 4);
}

/* the whole list: every place of the 5 is rewritten (rows, empty places, note), then the scroll bar */
static void draw_list_all(void)
{
    int i;
    for (i = 0; i < VIS; i++) {
        int p = top + i;
        if (p < lst_n) draw_slot(lst[p]);
        else draw_empty_slot(i, (lst_n == 0 && i == 1) ? "Wybierz typ filmu, żeby ustawić efekt" : 0);
    }
    draw_scrollbar();
}

static void paint_rows(void)                  /* everything except the background */
{
    draw_tabs();
    draw_sel(1);
    draw_sel(2);
    draw_info();
    draw_list_all();
}

#endif
