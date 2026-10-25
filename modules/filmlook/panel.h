/*
 * panel.h - how the panel looks: colours, positions and the drawing of every row. The camera (filmlook.c) and the PC
 * test of the screen (tools_host/uimock.c) use this same file, so the picture made on the PC is what the camera draws.
 *
 * Layout (720 x 480, margins 40 and 680, everything aligned to them):
 *   title (heavy retro serif)                                          version
 *   ---------------------------------------------------------------------
 *   type tabs: Bez efektu | C-41 | BW | Slajd | Zdjęcia | Ustawienia      (spread over the full width)
 *   brand (small, only shown: the choice is made among the films of the type)
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
/* Two looks, chosen in the last rows of the list: 0 = cream paper with dark brown ink, 1 = night (black, cream ink). Both are
 * one flat colour with one accent for what is selected and a teal for what was changed: no bars, no frames. The tones are
 * saturated on purpose: the LCD of the camera washes light colours out, a pale beige looked white on it. */
enum { PC_BG, PC_TEXT, PC_ACCENT, PC_SOFT, PC_DIM, PC_TRACK, PC_MOD, PC_N };
static const unsigned char pc_themes[2][PC_N][3] = {
  {  /* 0: cream paper, from the "70's Vibes" palette */
    { 240, 208, 160 },        /* PC_BG     cream #F0D0A0 (the palette's #F4DAB5, a little deeper: the LCD washes light tones out) */
    {  58,  36,  18 },        /* PC_TEXT   dark brown ink #3A2412 (#603C1F, darker) */
    { 159,  72,  21 },        /* PC_ACCENT rust #9F4815: the title, the selected row */
    { 110,  76,  46 },        /* PC_SOFT   #6E4C2E */
    { 181, 138,  94 },        /* PC_DIM    lines, switched-off rows #B58A5E */
    { 224, 186, 136 },        /* PC_TRACK  empty part of a bar #E0BA88 */
    {  29, 100, 113 },        /* PC_MOD    teal #1D6471: the part of a bar that differs from the default of the film */
  },
  {  /* 1: night, from the same palette */
    {  26,  23,  24 },        /* PC_BG     near black, warm #1A1718 (the palette's #3D393A, much darker) */
    { 244, 218, 181 },        /* PC_TEXT   cream #F4DAB5 */
    { 244, 161,   2 },        /* PC_ACCENT amber #F4A102: the title, the selected row */
    { 201, 170, 139 },        /* PC_SOFT   #C9AA8B */
    { 112,  96,  84 },        /* PC_DIM    #706054 */
    {  62,  55,  52 },        /* PC_TRACK  #3E3734 */
    {  95, 176, 160 },        /* PC_MOD    teal-green #5FB0A0 (the palette's #768F77 / #1D6471, lighter) */
  },
};
#define PAL_BASE   232                         /* first palette index used (the camera gets them back when the panel closes) */
#define PAL_COUNT  (PC_N + 2 * (PC_N - 1))     /* 7 colours + 2 half tones of each of the 6 inks = 19 */

static unsigned char pal_rgb[PAL_COUNT][3];    /* the colour of palette index PAL_BASE + i */

static void pal_build(void)
{
    const unsigned char (*pc)[3] = pc_themes[st_theme];
    int c, k, j;
    for (c = 0; c < PC_N; c++) for (j = 0; j < 3; j++) pal_rgb[c][j] = pc[c][j];
    for (c = 1; c < PC_N; c++)
        for (k = 1; k <= 2; k++)
            for (j = 0; j < 3; j++)
                pal_rgb[PC_N + 2 * (c - 1) + (k - 1)][j] = (unsigned char)(pc[0][j] + ((pc[c][j] - pc[0][j]) * k) / 3);
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
#define Y_TABS    80
#define Y_BRAND   126                 /* the maker: a little lower, about halfway between the tabs and the name */
#define Y_NAME    200                 /* the name of the film: large, with room above and below */
#define NAME_TOP  64
#define NAME_BOT  14
#define Y_KIND    240
#define Y_DESC    270
#define Y_RULE_B  290
#define SL_Y0     302                 /* top of the list (3 rows: the list scrolls) */
#define SL_PITCH  40
#define Y_RULE_C  436
#define Y_HINT    466

#define BAR_X     210
#define BAR_W     330
#define BAR_H     12
#define VAL_X     560


static void draw_rule(int y) { ui_fill(PC_DIM, M_L, y, M_R - M_L, 1); }

/* the line under the title: always there (1 px); while a JPG is made a thicker (3 px) part grows from the left.
 * permille 0..1000. Only the new piece is drawn when it grows, so nothing flickers. */
#define PROG_W (M_R - M_L)
static int prog_w = 0;                               /* width of the thick part that is on the screen now */
static void draw_prog(int permille)
{
    int w = permille <= 0 ? 0 : (permille >= 1000 ? PROG_W : (permille * PROG_W) / 1000);
    if (w < prog_w) { ui_fill(PC_BG, M_L, Y_RULE_A - 1, PROG_W, 3); draw_rule(Y_RULE_A); prog_w = 0; }
    if (w > prog_w) { ui_fill(PC_MOD, M_L + prog_w, Y_RULE_A - 1, w - prog_w, 3); prog_w = w; }
}

static void draw_hint(const char *msg)
{
    if (msg) sf_line(&sf_xxs, M_L, M_R, Y_HINT, 17, 5, msg, PC_ACCENT, SF_CENTER);
    else     sf_line(&sf_xxs, M_L, M_R3, Y_HINT, 17, 5, TR(S_HINT_KEYS), PC_SOFT, SF_JUST);
}

/* top left: how many JPGs are waiting / made now, or how long the last one took (empty = nothing to say) */
static void draw_status(const char *t)
{
    sf_line(&sf_xxs, M_L, M_L + 136, Y_TITLE, 17, 5, (t && t[0] && t[0] != '?') ? t : " ", PC_MOD, SF_LEFT);
}

static void draw_static(void)
{
    int r;
    for (r = 0; r < 480; r++) memset(ui_fb + r * ui_pitch, ui_ramp[PC_BG][3], 720);
    sf_line(&sf_title, 130, 590, Y_TITLE, 28, 3, "\001", PC_ACCENT, SF_CENTER);          /* centred on the screen (360) */
    sf_line(&sf_xxs, M_R - 90, M_R3, Y_TITLE, 17, 5, VERSION, PC_SOFT, SF_RIGHT);
    draw_rule(Y_RULE_A);
    prog_w = 0;                                       /* (the thick part is drawn again by the redraw that follows) */
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
static const char *tab_text(int i) { return TR(S_TAB_NONE + i); }     /* 0 no effect, 1..TYPE_COUNT the types, then "Zdjęcia" and "Ustawienia" (the texts follow each other in i18n.h) */

static void tab_layout(int *x, int *w)           /* x[i], w[i]: slot of tab i (x[n] = end of the last slot) */
{
    int i, n = NTABS, tot = 0, s0;
    for (i = 0; i < n; i++) { w[i] = sf_width(&sf_xsb, tab_text(i)) + TAB_GAP; tot += w[i]; }
    s0 = (M_L + M_R) / 2 - tot / 2;
    for (i = 0; i < n; i++) { x[i] = s0; s0 += w[i]; }
    x[n] = s0;
}

static void draw_tabs(void)
{
    int on = (row == 0), ct = cur_tab(), i, n = NTABS;
    int x[NTABS + 1], w[NTABS];
    tab_layout(x, w);
    draw_mark(on, Y_TABS - 22, 28);
    sf_line(&sf_xs, M_L, x[0], Y_TABS, 24, 7, " ", PC_BG, SF_LEFT);                  /* blank before and after the group */
    sf_line(&sf_xs, x[n], M_R3, Y_TABS, 24, 7, " ", PC_BG, SF_LEFT);
    for (i = 0; i < n; i++) {
        int sel = (i == ct);
        sf_line(sel ? &sf_xsb : &sf_xs, x[i], x[i] + w[i], Y_TABS, 24, 7, tab_text(i), sel ? (on ? PC_ACCENT : PC_TEXT) : PC_SOFT, SF_CENTER);
    }
}

/* row 1: the maker of the film (small, centred; it is only shown, it can not be chosen: the dial runs through all the films
 *        of the type),
 * row 2: the film name, large, the arrows right next to it */
static void draw_sel(int r)
{
    int on = (row == r) && !st_set && !st_view;
    int off = (st_film == 0) || st_set || st_view;       /* (the tabs "Ustawienia" and "Zdjęcia": the name row is only a heading, without arrows) */

    if (r == 1) {
        sf_line(&sf_xs, M_L, M_R3, Y_BRAND, 24, 7, (off || is_own(st_film)) ? " " : groups[films[st_film - 1].group].brand, PC_SOFT, SF_CENTER);     /* no maker for the own presets */
    } else {
        sf_run run[3];
        const char *name = st_set ? TR(S_TAB_SET) : (st_view ? TR(S_TAB_VIEW) : (off ? TR(S_TAB_NONE) : films[st_film - 1].name));
        const sf_font *f = &sf_big;
        int c = (st_set || st_view) ? PC_TEXT : (off ? PC_DIM : (on ? PC_ACCENT : PC_TEXT));
        int ca = off ? PC_BG : (on ? PC_ACCENT : PC_DIM);
        if (sf_width(f, name) > M_R - M_L - 100) f = &sf_mid;
        draw_mark(on, Y_NAME - 50, 62);
        run[0].f = &sf_big; run[0].s = off ? " " : "\xE2\x80\xB9 "; run[0].col = ca;
        run[1].f = f;       run[1].s = name;                      run[1].col = c;
        run[2].f = &sf_big; run[2].s = off ? " " : " \xE2\x80\xBA"; run[2].col = ca;
        sf_line_runs(M_L, M_R3, Y_NAME, NAME_TOP, NAME_BOT, run, 3, SF_CENTER);
    }
}

/* kind of film · ISO · number among the films of the type, then the description: both lines centred */
static void draw_info(void)
{
    char tmp[96], num[24];
    if (st_set) {                                        /* (the settings need no description: the two lines are blank) */
        sf_line(&sf_xs, M_L, M_R3, Y_KIND, 24, 7, " ", PC_BG, SF_CENTER);
        sf_line(&sf_xs, M_L, M_R3, Y_DESC, 24, 7, " ", PC_BG, SF_CENTER);
    } else if (st_view) {
        const sf_font *df = &sf_xs;
        const char *d = TR(S_VW_NOTE2);
        sf_line(&sf_xs, M_L, M_R3, Y_KIND, 24, 7, TR(S_VW_NOTE1), PC_ACCENT, SF_CENTER);
        if (sf_width(df, d) > M_R - M_L) df = &sf_xxs;
        sf_line(df, M_L, M_R3, Y_DESC, 24, 7, d, PC_TEXT, SF_CENTER);
    } else if (st_film > 0) {
        const film_t *f = &films[st_film - 1];
        const int ty = groups[f->group].type;
        const char *d = films[eff_film() - 1].desc[st_lang];
        const sf_font *kf = &sf_xs, *df = &sf_xs;
        sf_run run[2];
        if (f->iso > 0) snprintf(tmp, sizeof(tmp), "%s \xC2\xB7 ISO %d", f->kind[st_lang], f->iso);
        else            snprintf(tmp, sizeof(tmp), "%s", f->kind[st_lang]);
        snprintf(num, sizeof(num), "   \xC2\xB7   %d / %d", st_film - type_first(ty), type_films(ty));
        run[0].f = kf; run[0].s = tmp; run[0].col = PC_ACCENT;
        run[1].f = kf; run[1].s = num; run[1].col = PC_SOFT;
        if (sf_runs_width(run, 2) > M_R - M_L) { kf = &sf_xxs; run[0].f = kf; run[1].f = kf; }   /* very long kinds use the smallest size */
        sf_line_runs(M_L, M_R3, Y_KIND, 24, 7, run, 2, SF_CENTER);
        if (sf_width(df, d) > M_R - M_L) df = &sf_xxs;                          /* very long descriptions use the smallest size */
        sf_para(df, M_L, M_R3, Y_DESC, 24, 7, 30, 1, d, PC_TEXT, 1);
    } else {
        sf_line(&sf_xs, M_L, M_R3, Y_KIND, 24, 7, TR(S_OFF_NOTE), PC_ACCENT, SF_CENTER);
        sf_line(&sf_xs, M_L, M_R3, Y_DESC, 24, 7, " ", PC_BG, SF_CENTER);
    }
}

/* ------------------------------------------------------------------ list of 3 rows (it scrolls) */
static int slot_y(int r) { return SL_Y0 + (lst_pos(r) - top) * SL_PITCH + 27; }          /* baseline of row r */

static void list_row_frame(int r, int on, const char *label, int lcol)
{
    int yb = slot_y(r);
    draw_mark(on, yb - 22, 26);
    sf_line(&sf_sml, M_L, BAR_X - 12, yb, 25, 7, label, lcol, SF_LEFT);
}

/* x of a value on the bar */
static int bar_pos(int v, int lo, int hi) { return BAR_X + ((v - lo) * BAR_W) / (hi - lo); }

/* a slider row: bar and value in %. The part of the bar between the default of the slider and its value (what the
 * user changed) is teal; a small marker shows the default when it is neither end of the bar */
static void draw_slider(int r)                  /* r = 4 .. 13 */
{
    int j = r - 4, on = (row == r);
    int v = *sl_var(j), lo = sl_def[j].lo, hi = sl_def[j].hi, dv = sl_default[j];
    int yb = slot_y(r), x0, x1, ra, rb, zx, dx, vx, mx0 = 0, mx1 = 0, mra = 0, mrb = 0, mtop = 0;
    char tmp[16];

    list_row_frame(r, on, TR(S_SL_EFFECT + j), on ? PC_ACCENT : PC_TEXT);
    zx = bar_pos(0, lo, hi);
    dx = bar_pos(dv, lo, hi);
    vx = bar_pos(v, lo, hi);
    if (lo < 0) {                               /* two-sided: from the zero to the value */
        if (v > 0)      { x0 = zx; x1 = vx; ra = 0; rb = 1; }
        else if (v < 0) { x0 = vx; x1 = zx; ra = 1; rb = 0; }
        else            { x0 = x1 = zx; ra = rb = 0; }
    } else {
        x0 = BAR_X; x1 = vx; ra = rb = 1;
        if (v == lo) x1 = x0;
    }
    if (v != dv) {
        if (lo < 0) {                           /* two-sided (the default is the zero): the whole coloured part is the change */
            mx0 = x0; mx1 = x1; mra = ra; mrb = rb; mtop = 1;
        } else if (v > dv) {                    /* more than the default: the added part, on top of the filled part */
            mx0 = dx; mx1 = vx; mra = (dv == lo); mrb = 1; mtop = 1;
        } else {                                /* less than the default: the part taken away, behind the filled part */
            mx0 = vx - BAR_H / 2; mx1 = dx; mtop = 0;
            if (mx0 <= BAR_X) { mx0 = BAR_X; mra = 1; } else mra = 0;
            mrb = (dv == hi);
        }
    }
    ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, x0, x1, on ? PC_ACCENT : PC_SOFT, PC_TRACK, ra, rb,
           (dv > lo && dv < hi) ? dx : -1, BAR_X - 12, VAL_X, PC_SOFT, mx0, mx1, PC_MOD, mra, mrb, mtop);
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
        ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, x0, x1, on ? PC_ACCENT : PC_SOFT, PC_TRACK, ra, rb, cx, BAR_X - 12, VAL_X, PC_SOFT, 0, 0, PC_MOD, 0, 0, 0);   /* one marker: the normal position, like every other bar */
        if (st_push == 0) val = "normalnie";
        else { snprintf(pushv, sizeof(pushv), "%s%d EV", st_push > 0 ? "+" : "", st_push); val = pushv; }
    } else {
        ui_bar(BAR_X, yb - 14, BAR_W, BAR_H, cx, cx, PC_TRACK, PC_TRACK, 0, 0, -1, BAR_X - 12, VAL_X, PC_SOFT, 0, 0, PC_MOD, 0, 0, 0);
    }
    sf_line(&sf_sml, VAL_X, M_R3, yb, 25, 7, val, c, SF_RIGHT);
}

/* the rows of the tab "Ustawienia" that offer a choice: label at the left, "‹ value ›" at the right (no bar here, so the label
 * may be long: the two rectangles meet at CH_X) */
#define CH_X (M_L + 250)
static void draw_choice(int r, const char *label, const char *val, int lcol, int vcol)
{
    int on = (row == r), yb = slot_y(r);
    char tmp[80];
    draw_mark(on, yb - 22, 26);
    sf_line(&sf_sml, M_L, CH_X, yb, 25, 7, label, on ? PC_ACCENT : lcol, SF_LEFT);
    snprintf(tmp, sizeof(tmp), "\xE2\x80\xB9  %s  \xE2\x80\xBA", val);
    sf_line(&sf_sml, CH_X, M_R3, yb, 25, 7, tmp, on ? PC_ACCENT : vcol, SF_RIGHT);
}

/* JPG with the film look, developed in the background after every photo */
static void draw_jpg(void)
{
    draw_choice(JROW, TR(S_LBL_JPG), TR(S_JPG_OFF + st_jpg), PC_TEXT, st_jpg ? PC_TEXT : PC_SOFT);
}

/* where the JPG goes: its own folder, or in place of the camera's JPG (RAW+JPG or JPG shooting; with RAW alone there is no
 * JPG to replace and the file goes to FILMLOOK) */
static void draw_place(void)
{
    draw_choice(PROW, TR(S_LBL_PLACE), TR(S_PL_FOLDER + st_place), st_jpg ? PC_TEXT : PC_SOFT, st_jpg ? (st_place ? PC_MOD : PC_TEXT) : PC_SOFT);
}

/* the look of the panel: cream paper / night. The dial changes it at once (the whole panel is drawn again) */
static void draw_theme(void)
{
    draw_choice(TROW, TR(S_LBL_LOOK), TR(S_TH_CREAM + st_theme), PC_TEXT, PC_TEXT);
}

/* the language of the panel: the dial changes it at once (the whole panel is drawn again). The names are written in their own language */
static void draw_lang(void)
{
    draw_choice(LROW, TR(S_LBL_LANG), lang_names[st_lang], PC_TEXT, PC_TEXT);
}

/* a line with no bar, so it uses the whole width: the name at the left, what SET does at the right */
static void draw_reset_line(int r, const char *name, int n, int armed)
{
    int on = (row == r), yb = slot_y(r);
    char tmp[160];
    draw_mark(on, yb - 22, 26);
    if (armed)       snprintf(tmp, sizeof(tmp), "%s\t%s", name, TR(S_RST_CONF));
    else if (n > 0) { char num[96]; snprintf(num, sizeof(num), TR(S_RST_N), n); snprintf(tmp, sizeof(tmp), "%s\t%s", name, num); }
    else             snprintf(tmp, sizeof(tmp), "%s\t%s", name, TR(S_RST_NONE));
    sf_line(&sf_sml, M_L, M_R3, yb, 25, 7, tmp, on ? PC_ACCENT : (n > 0 ? PC_TEXT : PC_SOFT), SF_JUST);
}

/* last row under the sliders of a film: back to the defaults of THIS film. SET does it */
static void draw_reset(void)
{
    draw_reset_line(RROW, TR(S_RST_FILM), mod_n(), 0);
}

/* tab "Ustawienia", last row: every film back to its defaults. The first SET only asks (the line changes), the second one,
 * within a few seconds, does it */
static void draw_reset_all(void)
{
    draw_reset_line(AROW, TR(S_RST_ALL), films_mod_n(), reset_armed);
}

/* tab "Zdjęcia": the one row, SET opens the viewer */
static void draw_open(void)
{
    int on = (row == OROW), yb = slot_y(OROW);
    char tmp[160];
    draw_mark(on, yb - 22, 26);
    snprintf(tmp, sizeof(tmp), "%s\t%s", TR(S_VW_OPEN), TR(S_VW_SET));
    sf_line(&sf_sml, M_L, M_R3, yb, 25, 7, tmp, on ? PC_ACCENT : PC_TEXT, SF_JUST);
}

static void draw_slot(int r)
{
    int p = lst_pos(r);
    if (p < top || p >= top + VIS) return;           /* (also when the row does not exist for this film: p = -1) */
    if (r == 3) draw_push(); else if (r == JROW) draw_jpg(); else if (r == PROW) draw_place(); else if (r == TROW) draw_theme(); else if (r == LROW) draw_lang(); else if (r == RROW) draw_reset(); else if (r == AROW) draw_reset_all(); else if (r == OROW) draw_open(); else draw_slider(r);
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

/* the whole list: every place of the 3 is rewritten (rows, empty places, note), then the scroll bar */
static void draw_list_all(void)
{
    int i;
    for (i = 0; i < VIS; i++) {
        int p = top + i;
        if (p < lst_n) draw_slot(lst[p]);
        else draw_empty_slot(i, 0);
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
