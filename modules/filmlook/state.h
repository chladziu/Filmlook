/*
 * state.h - what the panel shows and changes: the choices of the user (film, push / pull, sliders), the rows of the
 * panel and the helper functions that read the film tables. No camera calls here, so the PC test of the screen
 * (tools_host/uimock.c) uses the very same file.
 */
#ifndef STATE_H
#define STATE_H

#include "i18n.h"
typedef char films_and_texts_have_the_same_languages[(FILM_LANGS == NLANG) ? 1 : -1];     /* films.h (kind / desc) and i18n.h (the panel) */

#define NSL   10                             /* sliders: rows 4..13 */
#define RROW  (4 + NSL)                      /* row 14: back to the default settings of THIS film (last row under the sliders) */
#define JROW  (RROW + 1)                     /* row 15: tab "Ustawienia": JPG with the effect (off / half size / full size) */
#define PROW  (JROW + 1)                     /* row 16: tab "Ustawienia": where the JPG goes (folder FILMLOOK / instead of the camera's JPG) */
#define TROW  (PROW + 1)                     /* row 17: tab "Ustawienia": the look of the panel (cream / night) */
#define LROW  (TROW + 1)                     /* row 18: tab "Ustawienia": the language of the panel */
#define AROW  (LROW + 1)                     /* row 19: tab "Ustawienia": every film back to its default settings (SET twice) */
#define NROWS (AROW + 1)                     /* 0 tabs, 1 maker (only shown), 2 film, 3 (not used any more), 4.. sliders (see sl_def), reset row of the film;
                                              * the rows 15..19 belong to the tab "Ustawienia" */
#define VIS   3                              /* rows of the list in a window of 3 (it scrolls) */

/* ------------------------------------------------------------------ state */
static int st_film = 0;                   /* 0 = Off, 1.. = index into films[] + 1 (always a base film) */
static int st_push = 0;                   /* push / pull of the film in stops, 0 = normal (see popts[]) */
static int st_strength = 100;             /* 0..100 */
static int st_grain = 100;                /* 0..200 */
static int st_contrast = 0;               /* -50..50 */
static int st_fade = 0;                   /* Matowość 0..100: lifted blacks */
static int st_warm = 0;                   /* Ciepło -100..100 */
static int st_shadow = 0;                 /* Cienie -100..100 */
static int st_high = 0;                   /* Światła -100..100 */
static int st_sat = 0;                    /* Nasycenie -100..100 */
static int st_vig = 0;                    /* Winieta 0..100 */
static int st_hal = 0;                    /* Halation 0..100: glow round bright lights (JPG only) */
static int st_jpg = 0;                    /* JPG z efektem: 0 off, 1 half size, 2 full size */
static int st_theme = 1;                  /* the look of the panel: 0 = cream paper, 1 = night (black) */
static int st_set = 0;                    /* the tab "Ustawienia" (the last one) is shown: the settings that count for all films. Not saved: the panel opens on the film */
static int reset_armed = 0;               /* the row "all films back to the defaults" got its first SET and waits for the second one */
static int st_place = 0;                  /* where the JPG goes: 0 = folder FILMLOOK, 1 = replaces the camera's own JPG (when there is one), 2 = also the preview inside the RAW */

/* the sliders of the panel: rows 4 .. 13 (their names are the texts S_SL_EFFECT + j of i18n.h) */
static const struct { short lo, hi, step; unsigned char signed_; } sl_def[NSL] = {
    { 0, 100, 5, 0 },  { 0, 200, 5, 0 },  { -50, 50, 5, 1 },          /* Efekt, Ziarno, Kontrast */
    { 0, 100, 5, 0 },                                                  /* Halacja (not shown) */
    { 0, 100, 5, 0 },  { -100, 100, 5, 1 },  { -100, 100, 5, 1 },      /* Matowość, Ciepło, Cienie */
    { -100, 100, 5, 1 }, { -100, 100, 5, 1 }, { 0, 100, 5, 0 },        /* Światła, Nasycenie, Winieta */
};

static int *sl_var(int j)
{
    switch (j) {
    case 0: return &st_strength;  case 1: return &st_grain;   case 2: return &st_contrast;
    case 3: return &st_hal;       case 4: return &st_fade;    case 5: return &st_warm;
    case 6: return &st_shadow;    case 7: return &st_high;    case 8: return &st_sat;
    default: return &st_vig;
    }
}

/* panel life cycle: opening and closing need the Canon GUI task to run, so they are done in the
 * shoot task (the key handler only requests them) */
#define UI_CLOSED   0
#define UI_OPENING  1
#define UI_OPEN     2
#define UI_CLOSING  3
static volatile int ui_state = UI_CLOSED;
static int row = 0;                       /* 0 type, 1 brand, 2 film, 3 push/pull, 4.. sliders */
static int top = 0;                       /* first row of the list that is shown (index into lst[], 0..lst_n-VIS) */
static int last_input_ms = 0;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* films[] is sorted by group (= brand inside one type: tools/preset2films.py), groups[] by type.
 * memo_group / memo_film remember the last brand of every type and the last film of every brand,
 * so that switching back and forth returns to where the user was */
static int memo_group[TYPE_COUNT];        /* relative to types[t].first_group */
static int memo_film[GROUP_COUNT];        /* relative to groups[g].first */

static void memo_init(void)
{
    int i;
    for (i = 0; i < TYPE_COUNT; i++) memo_group[i] = 0;
    for (i = 0; i < GROUP_COUNT; i++) memo_film[i] = 0;
}

/* the films of a type are one run in films[] (the brands of the type follow each other) */
static int type_first(int t) { return groups[types[t].first_group].first; }
static int type_films(int t) { const group_t *g = &groups[types[t].first_group + types[t].ngroups - 1]; return g->first + g->n - type_first(t); }

/* 0 = no effect, 1.. = type + 1 */
static int cur_type(void) { return st_film > 0 ? groups[films[st_film - 1].group].type + 1 : 0; }

/* the tab that is shown: the type of the film, or (the last one) "Ustawienia". The film stays chosen while the settings are open */
#define TAB_SET (TYPE_COUNT + 1)
static int cur_tab(void) { return st_set ? TAB_SET : cur_type(); }

/* push / pull: a base film may have variants (Tri-X +1, Portra 400 -1 ...) that are not in the Film list.
 * popts[f->pfirst .. +f->np-1] are its options sorted by shift, the normal film among them */
static int n_opts(void) { return st_film > 0 ? films[st_film - 1].np : 0; }

static int cur_opt(void)                      /* index of the option that is selected now */
{
    const film_t *f = &films[st_film - 1];
    int i, zero = 0;
    for (i = 0; i < f->np; i++) {
        if (popts[f->pfirst + i].shift == st_push) return i;
        if (popts[f->pfirst + i].shift == 0) zero = i;
    }
    return zero;
}

/* index into films[] + 1 of the film that is really used: the base film or its push / pull variant */
static int eff_film(void)
{
    if (st_film > 0 && n_opts() > 0) return 1 + popts[films[st_film - 1].pfirst + cur_opt()].film;
    return st_film;
}

static void fix_push(void)
{
    if (st_film == 0 || n_opts() == 0) st_push = 0;
    else st_push = popts[films[st_film - 1].pfirst + cur_opt()].shift;
}

static void remember(void)
{
    if (st_film > 0) {
        int g = films[st_film - 1].group;
        int t = groups[g].type;
        memo_group[t] = g - types[t].first_group;
        memo_film[g] = st_film - 1 - groups[g].first;
    }
}


/* ------------------------------------------------------------------ the sliders of every film
 * Every film has its own sliders. The film itself (its calibrated look) is never changed: what the user moves is kept
 * apart (film_v[]) and written to the settings file as the difference to the defaults. A film nobody touched has the
 * defaults (sl_default). st_* always hold the sliders of the film that is chosen now; when another film is chosen they
 * go back to film_v[] and the sliders of the other film are loaded. The own presets of the type "Własny" are films like
 * any other here (they simply start from the defaults and are meant to be changed). */
typedef struct { short strength, grain, contrast, hal, fade, warm, shadow, high, sat, vig; } vals_t;   /* order of sl_def */
static vals_t film_v[BASE_COUNT];
static int cur_slot = 0;                     /* film (1..) whose sliders are in st_* now, 0 = none */

/* default of every slider, in the order of sl_def: Efekt Ziarno Kontrast Halacja Matowość Ciepło Cienie Światła Nasycenie Winieta */
static const short sl_default[NSL] = { 100, 100, 0, 0, 0, 0, 0, 0, 0, 0 };

static short *vp(vals_t *v, int j) { return &((short *)v)[j]; }

static void vals_defaults(vals_t *v)
{
    int j;
    for (j = 0; j < NSL; j++) *vp(v, j) = sl_default[j];
}

static void vals_get(vals_t *v)
{
    v->strength = st_strength; v->grain = st_grain; v->contrast = st_contrast; v->hal = st_hal; v->fade = st_fade;
    v->warm = st_warm; v->shadow = st_shadow; v->high = st_high; v->sat = st_sat; v->vig = st_vig;
}

static void vals_put(const vals_t *v)
{
    st_strength = v->strength; st_grain = v->grain; st_contrast = v->contrast; st_hal = v->hal; st_fade = v->fade;
    st_warm = v->warm; st_shadow = v->shadow; st_high = v->high; st_sat = v->sat; st_vig = v->vig;
}

static void sets_init(void)
{
    int i;
    for (i = 0; i < BASE_COUNT; i++) vals_defaults(&film_v[i]);
    cur_slot = 0;
}

/* an own preset (type "Własny" = the last type): no effect strength, no push / pull */
static int is_own(int film)
{
    return film > 0 && groups[films[film - 1].group].type == TYPE_COUNT - 1;
}

/* film number (1..) of own preset slot k (1..5) */
static int own_film(int k)
{
    const group_t *g = &groups[types[TYPE_COUNT - 1].first_group];
    return 1 + g->first + clampi(k - 1, 0, g->n - 1);
}

/* st_film changed: the sliders of the old film go back to their place, those of the new film are loaded.
 * Returns 1 when the sliders were swapped */
static int switch_set(void)
{
    if (st_film == cur_slot) return 0;
    if (cur_slot > 0) vals_get(&film_v[cur_slot - 1]);
    cur_slot = st_film;
    if (cur_slot > 0) vals_put(&film_v[cur_slot - 1]);
    return 1;
}

/* how many sliders of the chosen film are not at their default (the effect strength does not exist for the own presets) */
static int mod_n(void)
{
    int j, n = 0;
    if (st_film == 0) return 0;
    for (j = is_own(st_film) ? 1 : 0; j < NSL; j++) if (j != 3 && *sl_var(j) != sl_default[j]) n++;     /* (3 = Halacja: not shown) */
    return n;
}

/* all sliders of the chosen film back to the defaults; returns how many were changed */
static int reset_film(void)
{
    int j, n = mod_n();
    if (st_film == 0) return 0;
    for (j = 0; j < NSL; j++) *sl_var(j) = sl_default[j];
    return n;
}

/* how many films have a slider that is not at its default (the one that is chosen now counts with its st_* values) */
static int films_mod_n(void)
{
    int i, j, n = 0;
    vals_t cur;
    vals_defaults(&cur);
    if (cur_slot > 0) vals_get(&cur);
    for (i = 0; i < BASE_COUNT; i++) {
        vals_t *v = (i + 1 == cur_slot) ? &cur : &film_v[i];
        for (j = is_own(i + 1) ? 1 : 0; j < NSL; j++)
            if (j != 3 && *vp(v, j) != sl_default[j]) { n++; break; }
    }
    return n;
}

/* every film back to the defaults (the chosen one too); returns how many films were changed */
static int reset_all(void)
{
    int i, n = films_mod_n();
    for (i = 0; i < BASE_COUNT; i++) vals_defaults(&film_v[i]);
    if (cur_slot > 0) vals_put(&film_v[cur_slot - 1]);
    return n;
}

/* the rows of the list: under a film (4.. sliders, reset row of the film) those that exist for the chosen film: no effect: none;
 * own preset: no effect strength. In the tab "Ustawienia": the four rows JROW..AROW. Since v0.10 there is no push / pull row and
 * no halation row (halation is switched off completely: HROW), the values stay in the settings file only so that older files are
 * still read */
#define HROW  (4 + 3)                        /* the Halacja slider (index 3 of sl_def) */
static int lst[NROWS], lst_n = 0;

static void list_build(void)
{
    int r, own = is_own(st_film);
    lst_n = 0;
    if (st_set) {
        for (r = JROW; r <= AROW; r++) lst[lst_n++] = r;
        top = 0;
        return;
    }
    if (st_film == 0) return;
    for (r = 4; r <= RROW; r++) {
        if (r == HROW) continue;
        if (own && r == 4) continue;
        lst[lst_n++] = r;
    }
    top = clampi(top, 0, lst_n > VIS ? lst_n - VIS : 0);
}

static int lst_pos(int r)                    /* index of row r in the list, -1 when it is not shown */
{
    int i;
    for (i = 0; i < lst_n; i++) if (lst[i] == r) return i;
    return -1;
}

#endif
