/*
 * persist.h - the settings file: the choice of the panel and the sliders of every film the user has changed.
 * Pure C on a text buffer (no camera calls), so the PC test (tools_host/test_persist.c) runs the very same code.
 *
 *   film=<full name>        the chosen film ("Off" = no effect)
 *   push=<stops>            push / pull of the chosen film
 *   jpg=<0..2>              JPG with the effect: off / half / full size
 *   mod=<film>;<10 numbers> the sliders of a film the user changed (order of sl_def); a film without this line has the
 *                           defaults. The films themselves live in the module and are never rewritten.
 * Files of the versions before 0.7 (one set of sliders for all films: strength= grain= ... and own1..own5) are still read.
 */
#ifndef PERSIST_H
#define PERSIST_H

static char cfg_buf[12 * 1024];

/* writes the settings text into cfg_buf, returns its length */
static int cfg_build(void)
{
    int i, j, n;
    if (cur_slot > 0) vals_get(&film_v[cur_slot - 1]);  /* the sliders in use go back to their film first */
    n = snprintf(cfg_buf, sizeof(cfg_buf), "film=%s\npush=%d\njpg=%d\nplace=%d\ntheme=%d\nlang=%d\n", st_film > 0 ? films[st_film - 1].full : "Off", st_push, st_jpg, st_place, st_theme, st_lang);
    for (i = 0; i < BASE_COUNT && n < (int)sizeof(cfg_buf) - 200; i++) {
        const short *v = (const short *)&film_v[i];
        int differs = 0;
        for (j = 0; j < NSL; j++) if (v[j] != sl_default[j]) differs = 1;
        if (!differs) continue;
        n += snprintf(cfg_buf + n, sizeof(cfg_buf) - n, "mod=%s;", films[i].full);
        for (j = 0; j < NSL && n < (int)sizeof(cfg_buf) - 8; j++)
            n += snprintf(cfg_buf + n, sizeof(cfg_buf) - n, "%d%c", v[j], j == NSL - 1 ? '\n' : ',');
    }
    if (n > (int)sizeof(cfg_buf) - 1) n = (int)sizeof(cfg_buf) - 1;
    return n;
}

static int parse_after(const char *s, const char *key, int *out)
{
    int kl = strlen(key);
    for (; *s; s++) {
        if (strncmp(s, key, kl) == 0 && s[kl] == '=') {
            const char *p = s + kl + 1;
            int neg = 0, v = 0, any = 0;
            if (*p == '-') { neg = 1; p++; }
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; }
            if (any) { *out = neg ? -v : v; return 1; }
        }
    }
    return 0;
}

/* "film=<full name>" (the name, not a number, so that adding films later never changes the saved choice) */
static void parse_film(const char *s)
{
    int i;
    st_film = 0;
    for (; *s; s++) {
        if (strncmp(s, "film=", 5) == 0) {
            const char *p = s + 5;
            for (i = 0; i < BASE_COUNT; i++) {
                int l = strlen(films[i].full);
                if (strncmp(p, films[i].full, l) == 0 && (p[l] == '\n' || p[l] == '\r' || p[l] == 0)) { st_film = i + 1; return; }
            }
            return;
        }
    }
}

/* the numbers after "key=" (up to n of them, separated by commas); returns how many were read */
static int parse_nums(const char *p, int *out, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int neg = 0, val = 0, any = 0;
        if (*p == '-') { neg = 1; p++; }
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; any = 1; }
        if (!any) return i;
        out[i] = neg ? -val : val;
        if (*p == ',') p++;
    }
    return n;
}

static void clamp_vals(vals_t *v, int film)
{
    int j;
    for (j = 0; j < NSL; j++) *vp(v, j) = (short)clampi(*vp(v, j), sl_def[j].lo, sl_def[j].hi);
    if (is_own(film)) v->strength = 100;                /* no effect strength for the own presets */
}

/* reads the settings text in cfg_buf (n bytes) and sets the choice, the sliders of all films and the slider set in use */
static void cfg_parse(int n)
{
    int mods = 0;
    vals_t legacy;
    int have_legacy = 0;
    sets_init();
    if (n > 0) {
        const char *s;
        cfg_buf[n] = 0;
        parse_film(cfg_buf);
        parse_after(cfg_buf, "push", &st_push);
        parse_after(cfg_buf, "jpg", &st_jpg);
        parse_after(cfg_buf, "place", &st_place);
        parse_after(cfg_buf, "theme", &st_theme);
        parse_after(cfg_buf, "lang", &st_lang);
        for (s = cfg_buf; *s; ) {                       /* "mod=<film>;<10 numbers>": the sliders of a film the user changed */
            const char *e = s;
            while (*e && *e != '\n') e++;
            if (strncmp(s, "mod=", 4) == 0) {
                int i;
                for (i = 0; i < BASE_COUNT; i++) {
                    int l = strlen(films[i].full);
                    if (strncmp(s + 4, films[i].full, l) == 0 && s[4 + l] == ';') {
                        int v[NSL], k;
                        if (parse_nums(s + 5 + l, v, NSL) == NSL) {
                            for (k = 0; k < NSL; k++) *vp(&film_v[i], k) = (short)v[k];
                            clamp_vals(&film_v[i], i + 1);
                            mods++;
                        }
                        break;
                    }
                }
            }
            s = *e ? e + 1 : e;
        }
        /* files of the versions before 0.7 kept one set of sliders for all films, and 'own1..own5' for the own presets:
         * they become the sliders of the chosen film / of the own presets (a film that has its own "mod=" line is not touched) */
        vals_defaults(&legacy);
        { static const char *const key[NSL] = { "strength", "grain", "contrast", "hal", "fade", "warm", "shadow", "high", "sat", "vig" };
          int j, v;
          for (j = 0; j < NSL; j++) { v = sl_default[j]; if (parse_after(cfg_buf, key[j], &v)) { *vp(&legacy, j) = (short)v; have_legacy = 1; } } }
        if (have_legacy && st_film > 0 && mods == 0) { film_v[st_film - 1] = legacy; clamp_vals(&film_v[st_film - 1], st_film); }
        { int k;
          for (k = 1; k <= 5; k++) {
              char key[8];
              int v[9], f = own_film(k);
              if (f <= 0) break;                       /* (no own presets since v0.11: the old lines are just ignored) */
              snprintf(key, sizeof(key), "own%d", k);
              { const char *q;
                for (q = cfg_buf; *q; q++)
                    if (strncmp(q, key, 4) == 0 && q[4] == '=' && (q == cfg_buf || q[-1] == '\n') && parse_nums(q + 5, v, 9) == 9) {
                        vals_t *w = &film_v[f - 1];
                        int d = 0, j;
                        for (j = 0; j < NSL; j++) if (*vp(w, j) != sl_default[j]) d = 1;
                        if (d) break;                    /* already has a "mod=" line */
                        w->grain = v[0]; w->contrast = v[1]; w->hal = v[2]; w->fade = v[3]; w->warm = v[4];
                        w->shadow = v[5]; w->high = v[6]; w->sat = v[7]; w->vig = v[8];
                        clamp_vals(w, f);
                        break;
                    } } } }
    }
    st_film = clampi(st_film, 0, BASE_COUNT);
    fix_push();
    remember();
    st_jpg = clampi(st_jpg, 0, 2);
    st_place = clampi(st_place, 0, 2);
    st_theme = clampi(st_theme, 0, 1);
    st_lang = clampi(st_lang, 0, NLANG - 1);
    cur_slot = 0;
    switch_set();                                       /* the sliders of the chosen film */
    list_build();
}

#endif
