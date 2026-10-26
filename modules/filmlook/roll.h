/*
 * roll.h - the film rolls ("Rolka filmu"). With the option on, a photo does not get its JPG at once: it is counted on the roll of
 * its film. When the roll is full (6 / 12 / 24 / 36 photos of that film) the whole roll is "sent to the lab" - the JPGs of all its
 * photos are made one after the other - and the film starts a new roll. The XMP of every photo is written at once, as always.
 * The camera counts how many rolls of every film were developed; the panel shows it under the film-roll icon (tab row).
 *
 * This file is only the data and its text file (ML/SETTINGS/FILMLOOK.ROL): pure C, no camera calls, so the PC test
 * (tools_host/test_roll.c) and the screen test (tools_host/uimock.c) run the very same code.
 *
 *   FLROLL 1
 *   d=<film full name>;<rolls developed>
 *   p=<0 waits in the roll | 1 to be developed>;<film full name>;<photo number>;<JPG size 0..3>;<DCIM folder>;<file prefix>;<9 slider values>
 */
#ifndef ROLL_H
#define ROLL_H

#define RL_SIZES 5
static const unsigned char rl_size_tab[RL_SIZES] = { 0, 6, 12, 24, 36 };
static int st_roll = 0;                    /* Rolka filmu: 0 off, 1..4 = 6 / 12 / 24 / 36 photos on a roll */

static int roll_size(void) { return rl_size_tab[st_roll < 0 ? 0 : (st_roll >= RL_SIZES ? RL_SIZES - 1 : st_roll)]; }

#define RL_MAXP   120                      /* photos that can wait / be developed at the same time (all films together) */
#define RL_WAIT   0                        /* on the roll, waiting for the roll to be full */
#define RL_LAB    1                        /* the roll was sent: its JPG is still to be made */
#define RL_QUEUED 2                        /* its JPG is in the develop queue of the camera right now (written to the file as RL_LAB) */
#define RL_NLK    9                        /* sliders kept with a photo (Efekt, Ziarno, Kontrast, Matowość, Ciepło, Cienie, Światła, Nasycenie, Winieta) */

typedef struct {
    int id, number;
    short film;                            /* 1.. = index into films[] + 1 (a base film) */
    unsigned char st, mode;                /* RL_*, JPG size 0..3 (0 = the photo is only counted: no JPG) */
    char dir[40], prefix[8];
    short lk[RL_NLK];
} rl_frame_t;

static rl_frame_t rl_f[RL_MAXP];
static int rl_n = 0;
static int rl_done[BASE_COUNT];            /* rolls developed, per film */
static int rl_next_id = 1;
static volatile int rl_ver = 0;            /* counts every change: the panel redraws its list when it moves */

static void rl_reset(void)
{
    int i;
    rl_n = 0; rl_next_id = 1;
    for (i = 0; i < BASE_COUNT; i++) rl_done[i] = 0;
    memset(rl_f, 0, sizeof rl_f);
    rl_ver++;
}

/* photos of a film (0 = all films): RL_WAIT = those on the roll, RL_LAB = those that are to be developed (queued ones too), -1 = all */
static int rl_count(int film, int st)
{
    int i, n = 0;
    for (i = 0; i < rl_n; i++) {
        const rl_frame_t *p = &rl_f[i];
        if (film && p->film != film) continue;
        if (st == RL_WAIT && p->st != RL_WAIT) continue;
        if (st == RL_LAB && p->st == RL_WAIT) continue;
        n++;
    }
    return n;
}

/* the roll must stay visible (its icon in the tab row) while the option is on, and while anything is left of an older roll */
static int rl_visible(void) { return st_roll > 0 || rl_n > 0; }

static void rl_copy(char *dst, int cap, const char *s)
{
    int i;
    for (i = 0; i < cap - 1 && s && s[i]; i++) dst[i] = s[i];
    dst[i] = 0;
}

/* a photo joins the roll of its film; returns its id, 0 = it can not (no room, or not a base film): the caller makes its JPG at once */
static int rl_add(int film, int number, int mode, const char *dir, const char *prefix, const short *lk)
{
    rl_frame_t *p;
    int i;
    if (film < 1 || film > BASE_COUNT || rl_n >= RL_MAXP) return 0;
    p = &rl_f[rl_n++];
    memset(p, 0, sizeof *p);
    p->id = rl_next_id++; p->film = (short)film; p->number = number; p->mode = (unsigned char)mode; p->st = RL_WAIT;
    rl_copy(p->dir, sizeof p->dir, dir);
    rl_copy(p->prefix, sizeof p->prefix, prefix);
    for (i = 0; i < RL_NLK; i++) p->lk[i] = lk[i];
    rl_ver++;
    return p->id;
}

/* the roll of a film (0 = every film) goes to the lab: its photos are to be developed, the film counts one more roll.
 * Returns how many photos were sent */
static int rl_release(int film)
{
    int f, i, n = 0;
    for (f = 1; f <= BASE_COUNT; f++) {
        int k = 0;
        if (film && f != film) continue;
        for (i = 0; i < rl_n; i++) if (rl_f[i].film == f && rl_f[i].st == RL_WAIT) { rl_f[i].st = RL_LAB; k++; }
        if (k) { rl_done[f - 1]++; n += k; }
    }
    if (n) rl_ver++;
    return n;
}

/* the next photo to develop (the oldest one that is not yet in the queue): copied to *out and marked as queued; 0 = none */
static int rl_take(rl_frame_t *out)
{
    int i;
    for (i = 0; i < rl_n; i++)
        if (rl_f[i].st == RL_LAB) { rl_f[i].st = RL_QUEUED; *out = rl_f[i]; rl_ver++; return 1; }
    return 0;
}

/* a photo is developed (or can not be): it leaves the list. Returns 1 when it was there */
static int rl_remove(int id)
{
    int i;
    for (i = 0; i < rl_n; i++) {
        if (rl_f[i].id != id) continue;
        for (; i + 1 < rl_n; i++) rl_f[i] = rl_f[i + 1];
        memset(&rl_f[rl_n - 1], 0, sizeof(rl_frame_t));
        rl_n--;
        rl_ver++;
        return 1;
    }
    return 0;
}

/* a photo that was queued when the camera stopped has no job any more: it is to be developed again */
static void rl_requeue(void)
{
    int i;
    for (i = 0; i < rl_n; i++) if (rl_f[i].st == RL_QUEUED) rl_f[i].st = RL_LAB;
}

/* the films that have something on the panel: a started roll, a roll in the lab or developed rolls */
typedef struct { short film, wait, lab, done; } rl_ent_t;
static rl_ent_t rl_ent[BASE_COUNT];
static int rl_ent_n = 0;

static void rl_entries(void)
{
    int f;
    rl_ent_n = 0;
    for (f = 1; f <= BASE_COUNT; f++) {
        int w = rl_count(f, RL_WAIT), l = rl_count(f, RL_LAB), d = rl_done[f - 1];
        if (w || l || d) {
            rl_ent_t *e = &rl_ent[rl_ent_n++];
            e->film = (short)f; e->wait = (short)w; e->lab = (short)l; e->done = (short)d;
        }
    }
}

/* everything forgotten: the counters, the waiting photos and those that were to be developed. Returns the number of films that had something */
static int rl_clear(void)
{
    int n;
    rl_entries();
    n = rl_ent_n;
    rl_reset();
    return n;
}

/* ------------------------------------------------------------------ the file */
static char rl_buf[RL_MAXP * 112 + 1024];

/* writes the text of the file into rl_buf, returns its length */
static int rl_build(void)
{
    int i, j, n;
    n = snprintf(rl_buf, sizeof(rl_buf), "FLROLL 1\n");
    for (i = 0; i < BASE_COUNT && n < (int)sizeof(rl_buf) - 100; i++)
        if (rl_done[i] > 0) n += snprintf(rl_buf + n, sizeof(rl_buf) - n, "d=%s;%d\n", films[i].full, rl_done[i]);
    for (i = 0; i < rl_n && n < (int)sizeof(rl_buf) - 160; i++) {
        const rl_frame_t *p = &rl_f[i];
        n += snprintf(rl_buf + n, sizeof(rl_buf) - n, "p=%d;%s;%d;%d;%s;%s;", p->st == RL_WAIT ? 0 : 1, films[p->film - 1].full, p->number, p->mode, p->dir, p->prefix);
        for (j = 0; j < RL_NLK; j++) n += snprintf(rl_buf + n, sizeof(rl_buf) - n, "%d%c", p->lk[j], j == RL_NLK - 1 ? '\n' : ',');
    }
    if (n > (int)sizeof(rl_buf) - 1) n = (int)sizeof(rl_buf) - 1;
    return n;
}

/* the film whose full name starts at s and ends with the character `end`: index 1.. or 0; *pp is moved behind the end character */
static int rl_film_at(const char **pp, char end)
{
    int i;
    for (i = 0; i < BASE_COUNT; i++) {
        int l = (int)strlen(films[i].full);
        if (strncmp(*pp, films[i].full, l) == 0 && (*pp)[l] == end) { *pp += l + 1; return i + 1; }
    }
    return 0;
}

/* a whole number at *pp (an optional minus first); the character that follows is skipped. Returns 1 when there was a number */
static int rl_num(const char **pp, int *out)
{
    const char *p = *pp;
    int neg = 0, v = 0, any = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { if (v < 100000000) v = v * 10 + (*p - '0'); p++; any = 1; }
    if (!any) return 0;
    *out = neg ? -v : v;
    if (*p && *p != '\n') p++;
    *pp = p;
    return 1;
}

/* a text up to the next `end` character (at most cap - 1 characters are kept) */
static int rl_str(const char **pp, char end, char *dst, int cap)
{
    const char *p = *pp;
    int i = 0;
    while (*p && *p != end && *p != '\n') { if (i < cap - 1) dst[i++] = *p; p++; }
    dst[i] = 0;
    if (*p != end) return 0;
    *pp = p + 1;
    return 1;
}

/* reads the text in rl_buf (n bytes); what is wrong in a line is skipped */
static void rl_parse(int n)
{
    const char *s;
    rl_reset();
    if (n <= 0) return;
    if (n > (int)sizeof(rl_buf) - 1) n = (int)sizeof(rl_buf) - 1;
    rl_buf[n] = 0;
    if (strncmp(rl_buf, "FLROLL 1", 8) != 0) return;
    for (s = rl_buf; *s; ) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        if (s[0] == 'd' && s[1] == '=') {
            const char *p = s + 2;
            int f = rl_film_at(&p, ';'), d = 0;
            if (f > 0 && rl_num(&p, &d)) rl_done[f - 1] = d < 0 ? 0 : (d > 9999 ? 9999 : d);
        } else if (s[0] == 'p' && s[1] == '=') {
            const char *p = s + 2;
            int st = 0, f, num = 0, mode = 0, k, ok = 1;
            char dir[40], pre[8];
            short lk[RL_NLK];
            if (!rl_num(&p, &st)) ok = 0;
            if (ok && (f = rl_film_at(&p, ';')) == 0) ok = 0;
            if (ok && !rl_num(&p, &num)) ok = 0;
            if (ok && !rl_num(&p, &mode)) ok = 0;
            if (ok && !rl_str(&p, ';', dir, sizeof dir)) ok = 0;
            if (ok && !rl_str(&p, ';', pre, sizeof pre)) ok = 0;
            for (k = 0; ok && k < RL_NLK; k++) { int v = 0; if (rl_num(&p, &v)) lk[k] = (short)v; else ok = 0; }
            if (ok && dir[0] && pre[0] && num > 0 && mode >= 0 && mode <= 3) {
                int id = rl_add(f, num, mode, dir, pre, lk);
                if (id && st) rl_f[rl_n - 1].st = RL_LAB;
            }
        }
        s = *e ? e + 1 : e;
    }
    rl_ver++;
}

#endif
