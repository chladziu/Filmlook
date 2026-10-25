/*
 * state.h - what the panel shows and changes: the choices of the user (film, push / pull, sliders), the rows of the
 * panel and the helper functions that read the film tables. No camera calls here, so the PC test of the screen
 * (tools_host/uimock.c) uses the very same file.
 */
#ifndef STATE_H
#define STATE_H

#define NSL   10                             /* sliders: rows 4..13 */
#define JROW  (4 + NSL)                      /* row 14: JPG with the effect (off / half size / full size) */
#define NROWS (JROW + 1)                     /* 0 type, 1 brand, 2 film, 3 push/pull, 4.. sliders (see sl_def), JPG row */
#define VIS   5                              /* rows 3..14 scroll in a window of 5 */

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

/* the sliders of the panel: rows 4 .. 13 */
static const struct { const char *name; short lo, hi, step; unsigned char signed_; } sl_def[NSL] = {
    { "Efekt",     0, 100, 5, 0 },  { "Ziarno",     0, 200, 5, 0 },  { "Kontrast", -50, 50, 5, 1 },
    { "Halacja",   0, 100, 5, 0 },
    { "Matowość",  0, 100, 5, 0 },  { "Ciepło",  -100, 100, 5, 1 },  { "Cienie", -100, 100, 5, 1 },
    { "Światła", -100, 100, 5, 1 }, { "Nasycenie", -100, 100, 5, 1 }, { "Winieta", 0, 100, 5, 0 },
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

/* 0 = no effect, 1.. = type + 1 */
static int cur_type(void) { return st_film > 0 ? groups[films[st_film - 1].group].type + 1 : 0; }

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


/* ------------------------------------------------------------------ own presets and the visible list
 * The sliders belong to a "set": set 0 is used with all films, sets 1..OWN_SLOTS belong to the own presets of the type
 * "Własny" (every preset keeps its own sliders). st_* always hold the set of the film that is chosen now; when the choice
 * moves to another set, the sliders are swapped. */
#define OWN_SLOTS 5
typedef struct { short strength, grain, contrast, hal, fade, warm, shadow, high, sat, vig; } vals_t;
static vals_t set_v[1 + OWN_SLOTS];
static int cur_set = 0;

static void vals_defaults(vals_t *v)
{
    v->strength = 100; v->grain = 100; v->contrast = 0; v->hal = 0; v->fade = 0;
    v->warm = 0; v->shadow = 0; v->high = 0; v->sat = 0; v->vig = 0;
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
    for (i = 0; i < 1 + OWN_SLOTS; i++) vals_defaults(&set_v[i]);
    cur_set = 0;
}

/* the set the chosen film uses: 0, or 1.. for the own preset slots (type "Własny" = the last type) */
static int set_of(int film)
{
    if (film > 0) {
        const group_t *g = &groups[films[film - 1].group];
        if (g->type == TYPE_COUNT - 1) return 1 + clampi(film - 1 - g->first, 0, OWN_SLOTS - 1);
    }
    return 0;
}

/* st_film changed: swap the sliders when the film belongs to another set. Returns 1 when they were swapped */
static int switch_set(void)
{
    int ns = set_of(st_film);
    if (ns == cur_set) return 0;
    vals_get(&set_v[cur_set]);
    cur_set = ns;
    vals_put(&set_v[cur_set]);
    return 1;
}

/* the rows of the list under the film (3 push / pull, 4.. sliders, JPG row) that exist for the chosen film:
 * no effect: none; own preset: no push / pull and no effect strength */
static int lst[NROWS], lst_n = 0;

static void list_build(void)
{
    int r, own = (st_film > 0 && cur_set > 0);
    lst_n = 0;
    if (st_film == 0) return;
    for (r = 3; r < NROWS; r++) {
        if (own && (r == 3 || r == 4)) continue;
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
