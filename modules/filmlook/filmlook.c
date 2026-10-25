/*
 * filmlook.c - film look simulation for Canon EOS 5D Mark II (Magic Lantern module)
 *
 *  TRASH opens ONE full-screen panel: type (colour / black & white / slide) -> brand -> film + settings
 *  (Push/Pull slider when the film has push / pull variants, effect strength, grain, contrast and the "own look"
 *  sliders: fade, warmth, shadows, highlights, saturation, vignette, halation, and the JPG switch - a list of 4 rows
 *  that scrolls).
 *    both dials and joystick left/right = change value of the chosen row, joystick up/down = next row,
 *    SET = save, TRASH / INFO / MENU = save & close, half-shutter = close and shoot.
 *  After every photo an IMG_xxxx.xmp sidecar is written (Lightroom / Camera Raw) and, when "JPG z efektem" is on,
 *  a finished JPG with the film look is developed in the background into the FILMLOOK folder of the card.
 *  RAW / JPEG files of the camera are never touched. No Canon property is written.
 *
 *  While the panel is open Canon's own drawing is switched off (canon_gui_disable_front_buffer),
 *  exactly like ML's own menu does, so nothing flickers.
 */
#include <module.h>
#include <dryos.h>
#include <property.h>
#include <bmp.h>
#include <fileprefix.h>
#include <config.h>
#include <powersave.h>

#include "films.h"
#include "xmp.h"
#include "develop.h"

#define VERSION "v0.6"
#define LOG_PATH      "ML/LOGS/FILMLOOK.LOG"
#define SETTINGS_PATH "ML/SETTINGS/FILMLOOK.TXT"
extern int gui_state;
extern int lv;
extern int set_config_var(const char *name, int new_value);
extern int get_config_var(const char *name);

#include "state.h"

static void cur_look(look_t *L)
{
    L->strength = st_strength; L->grain = st_grain; L->contrast = st_contrast; L->fade = st_fade; L->warm = st_warm;
    L->shadow = st_shadow; L->high = st_high; L->sat = st_sat; L->vig = st_vig; L->hal = st_hal;
}

/* ------------------------------------------------------------------ log */
static void flog(const char *a, int v)
{
    char line[128];
    snprintf(line, sizeof(line), "%d  %s %d\n", get_ms_clock(), a, v);
    FILE *f = FIO_CreateFileOrAppend(LOG_PATH);
    if (f) { FIO_WriteFile(f, line, strlen(line)); FIO_CloseFile(f); }
}

static void flog_s(const char *a, const char *s)
{
    char line[160];
    snprintf(line, sizeof(line), "%d  %s %s\n", get_ms_clock(), a, s);
    FILE *f = FIO_CreateFileOrAppend(LOG_PATH);
    if (f) { FIO_WriteFile(f, line, strlen(line)); FIO_CloseFile(f); }
}

/* key events are collected in RAM and written to the log when the panel closes,
 * so that the (slow) card access never delays the camera's button handling */
#define KLOG_N 96
static int klog_t[KLOG_N];
static int klog_k[KLOG_N];
static int klog_n = 0;

static void klog_add(int key)
{
    if (klog_n < KLOG_N) { klog_t[klog_n] = get_ms_clock(); klog_k[klog_n] = key; klog_n++; }
}

static void klog_flush(void)
{
    static char buf[KLOG_N * 24 + 32];
    int i, n = 0;
    if (klog_n <= 0) return;
    n += snprintf(buf + n, sizeof(buf) - n, "%d  keys while panel open (time_ms key):\n", get_ms_clock());
    for (i = 0; i < klog_n && n < (int)sizeof(buf) - 24; i++)
        n += snprintf(buf + n, sizeof(buf) - n, "  %d %d\n", klog_t[i], klog_k[i]);
    klog_n = 0;
    {
        FILE *f = FIO_CreateFileOrAppend(LOG_PATH);
        if (f) { FIO_WriteFile(f, buf, n); FIO_CloseFile(f); }
    }
}

/* ------------------------------------------------------------------ settings file */
static void save_state(void)
{
    static char t[900];
    const vals_t *a;
    int k, n;
    vals_get(&set_v[cur_set]);                          /* the sliders in use go back to their set first */
    a = &set_v[0];
    snprintf(t, sizeof(t), "film=%s\npush=%d\nstrength=%d\ngrain=%d\ncontrast=%d\nfade=%d\nwarm=%d\nshadow=%d\nhigh=%d\nsat=%d\nvig=%d\nhal=%d\njpg=%d\n",
             st_film > 0 ? films[st_film - 1].full : "Off", st_push, a->strength, a->grain, a->contrast,
             a->fade, a->warm, a->shadow, a->high, a->sat, a->vig, a->hal, st_jpg);
    for (k = 1; k <= OWN_SLOTS; k++) {                  /* the own presets: grain, contrast, halation, fade, warm, shadow, high, sat, vig */
        const vals_t *v = &set_v[k];
        n = strlen(t);
        snprintf(t + n, sizeof(t) - n, "own%d=%d,%d,%d,%d,%d,%d,%d,%d,%d\n", k, v->grain, v->contrast, v->hal, v->fade, v->warm,
                 v->shadow, v->high, v->sat, v->vig);
    }
    FILE *f = FIO_CreateFile(SETTINGS_PATH);
    if (f) { FIO_WriteFile(f, t, strlen(t)); FIO_CloseFile(f); }
    else flog("save_state: cannot create file", 0);
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

/* "own3=g,c,h,f,w,s,h,s,v": the nine numbers of an own preset, -999 where the file has nothing */
static void parse_own(const char *buf, int k, vals_t *v)
{
    char key[8];
    int n[9], i;
    const char *s;
    snprintf(key, sizeof(key), "own%d=", k);
    for (s = buf; *s; s++) {
        if (strncmp(s, key, 5) == 0 && (s == buf || s[-1] == '\n')) {
            const char *p = s + 5;
            for (i = 0; i < 9; i++) {
                int neg = 0, val = 0, any = 0;
                if (*p == '-') { neg = 1; p++; }
                while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; any = 1; }
                if (!any) return;
                n[i] = neg ? -val : val;
                if (*p == ',') p++;
            }
            v->grain = n[0]; v->contrast = n[1]; v->hal = n[2]; v->fade = n[3]; v->warm = n[4];
            v->shadow = n[5]; v->high = n[6]; v->sat = n[7]; v->vig = n[8];
            return;
        }
    }
}

static void load_state(void)
{
    static char buf[1200];
    int n = read_file(SETTINGS_PATH, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        parse_film(buf);
        parse_after(buf, "push", &st_push);
        parse_after(buf, "strength", &st_strength);
        parse_after(buf, "grain", &st_grain);
        parse_after(buf, "contrast", &st_contrast);
        parse_after(buf, "fade", &st_fade);
        parse_after(buf, "warm", &st_warm);
        parse_after(buf, "shadow", &st_shadow);
        parse_after(buf, "high", &st_high);
        parse_after(buf, "sat", &st_sat);
        parse_after(buf, "vig", &st_vig);
        parse_after(buf, "hal", &st_hal);
        parse_after(buf, "jpg", &st_jpg);
        { int k; for (k = 1; k <= OWN_SLOTS; k++) parse_own(buf, k, &set_v[k]); }
    }
    st_film = clampi(st_film, 0, BASE_COUNT);
    fix_push();
    remember();
    st_strength = clampi(st_strength, 0, 100);
    st_grain = clampi(st_grain, 0, 200);
    st_contrast = clampi(st_contrast, -50, 50);
    { int j; for (j = 0; j < NSL; j++) *sl_var(j) = clampi(*sl_var(j), sl_def[j].lo, sl_def[j].hi); }
    st_jpg = clampi(st_jpg, 0, 2);
    vals_get(&set_v[0]);                                /* what the old keys held belongs to set 0 */
    cur_set = 0;
    { int k, j; for (k = 1; k <= OWN_SLOTS; k++) {      /* the own presets: same limits, effect strength fixed at 100 */
        vals_put(&set_v[k]);
        st_strength = 100;
        for (j = 0; j < NSL; j++) *sl_var(j) = clampi(*sl_var(j), sl_def[j].lo, sl_def[j].hi);
        vals_get(&set_v[k]);
    } }
    vals_put(&set_v[0]);
    switch_set();                                       /* the chosen film may be an own preset */
    list_build();
}

/* ------------------------------------------------------------------ drawing */
/* panel.h draws every row (serif letters, rounded bars) into the screen buffer; the 16 colours it needs are palette
 * entries PAL_BASE.. of the camera, programmed while the panel is open and given back to Canon when it closes. */
#include "panel.h"

/* palette entry = opacity, Y, U, V (signed bytes); white is Y 235, so Y is scaled by 235/255 */
static int pal_entry(const unsigned char *c, int opacity)
{
    int r = c[0], g = c[1], b = c[2];
    int y = (299 * r + 587 * g + 114 * b) / 1000;
    int u = ((b - y) * 564) / 1000;
    int v = ((r - y) * 713) / 1000;
    y = (y * 235) / 255; u = (u * 235) / 255; v = (v * 235) / 255;
    u = u < -127 ? -127 : (u > 127 ? 127 : u);
    v = v < -127 ? -127 : (v > 127 ? 127 : v);
    return ((opacity & 0xFF) << 24) | ((y & 0xFF) << 16) | ((u & 0xFF) << 8) | (v & 0xFF);
}

static void pal_apply(void)
{
    int i, opacity = (LCD_Palette[3 * COLOR_BLACK + 2] >> 24) & 0xFF;
    if (opacity == 0) opacity = 3;                      /* fully opaque */
    if (!bmp_is_on()) return;
    for (i = 0; i < PAL_COUNT; i++) {
        int e = pal_entry(pal_rgb[i], opacity);
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i)], e);
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i) + 0x300], e);
    }
}

static void pal_restore(void)                           /* back to what Canon had there */
{
    int i;
    for (i = 0; i < PAL_COUNT; i++) {
        int e = LCD_Palette[3 * (PAL_BASE + i) + 2];
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i)], e);
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i) + 0x300], e);
    }
}

/* what has to be redrawn (set from the key handler, handled in the shoot task) */
#define D_ROW(r) (1 << (r))          /* rows 0..14 */
#define D_INFO   (1 << 15)           /* kind / description block under the film row */
#define D_BAR    (1 << 16)           /* scroll bar of the list */
#define D_LISTALL (1 << 17)          /* the whole list: it scrolled, or the rows / the sliders changed with the film */
static volatile int dirty = 0;

/* the drawing functions write straight into the screen buffer of the camera (call with BMP_LOCK held) */
static void ui_attach(void)
{
    ui_fb = bmp_vram();
    ui_pitch = BMPPITCH;
}

static int msg_until = 0;            /* ms clock: show the "saved" message until then */
static volatile int want_save = 0;

static void redraw_dirty(void)
{
    int d = dirty;
    if (want_save) {
        want_save = 0;
        save_state();
        msg_until = get_ms_clock() + 1200;
        BMP_LOCK ( ui_attach(); draw_hint("Zapisano"); )
        flog_s("settings saved, film", st_film > 0 ? films[eff_film() - 1].full : "Off");
    } else if (msg_until && get_ms_clock() > msg_until) {
        msg_until = 0;
        BMP_LOCK ( ui_attach(); draw_hint(0); )
    }
    if (!d) return;
    dirty &= ~d;
    BMP_LOCK
    (
        ui_attach();
        if (d & D_ROW(0))  draw_tabs();
        if (d & D_ROW(1))  draw_sel(1);
        if (d & D_ROW(2))  draw_sel(2);
        if (d & D_INFO)    draw_info();
        if (d & D_LISTALL) draw_list_all();
        else { int r; for (r = 3; r < NROWS; r++) if (d & D_ROW(r)) draw_slot(r); }
    )
}

/* ------------------------------------------------------------------ open / close */
/* the Canon INFO screen shows ML's own status overlay (clock, free space, battery, shots left).
 * "Global Draw" off keeps that overlay away, so only our panel is ever on the screen */
static void kill_ml_overlay(void)
{
    if (get_config_var("global.draw") != 0) set_config_var("global.draw", 0);
}

/* Canon GUI mode variable of the 5D2 firmware 2.1.2 (same address as CURRENT_GUI_MODE in the ML platform files) */
#define CUR_GUI_MODE (*(volatile int *)0x37F0)
extern void SetGUIRequestMode(int mode);
extern void msleep(int ms);

static void paint_all(void)
{
    BMP_LOCK
    (
        ui_attach();
        draw_static();
        dirty = 0;
        paint_rows();
    )
}

/* Same trick as the ML menu: in photo mode the Canon INFO screen does not pass the joystick and part of the
 * dial clicks to ML, so the camera is switched to its (invisible) menu mode, where all buttons arrive.
 * The Canon menu that appears for a moment is hidden by switching the bitmap layer off meanwhile. */
static void do_open(void)
{
    int i;
    kill_ml_overlay();
    row = (st_film > 0) ? 2 : 0;                        /* start on the film row when a film is chosen */
    top = 0;
    list_build();
    klog_n = 0;
    msg_until = 0;
    want_save = 0;
    last_input_ms = get_ms_clock();
    if (!lv && CUR_GUI_MODE != 2) {
        bmp_off();
        SetGUIRequestMode(2);
        for (i = 0; i < 40 && CUR_GUI_MODE != 2; i++) msleep(25);
        msleep(280);                                   /* Canon draws its menu */
    }
    canon_gui_disable_front_buffer();
    paint_all();
    msleep(60);
    canon_gui_disable_front_buffer();
    paint_all();
    bmp_on();
    pal_apply();
    last_input_ms = get_ms_clock();
    ui_state = UI_OPEN;
    flog("screen opened, film", st_film);
}

static void do_close(void)
{
    dirty = 0;
    want_save = 0;
    save_state();
    klog_flush();
    pal_restore();
    if (CUR_GUI_MODE != 0) {
        bmp_off();
        SetGUIRequestMode(0);
        msleep(150);
    }
    canon_gui_enable_front_buffer(1);
    msleep(150);
    bmp_on();
    flog("screen closed, film", st_film);
    ui_state = UI_CLOSED;
}

/* the chosen film changed: keep the choice, swap the sliders when it moves to / from an own preset, rebuild the list.
 * Returns the dirty bits of the list */
static int film_changed(void)
{
    int old_n = lst_n, old_set = cur_set, swapped;
    st_push = 0;
    swapped = switch_set();
    list_build();
    if (swapped || old_n != lst_n || (old_set != cur_set)) { top = 0; return D_LISTALL; }
    return D_ROW(3);
}

static void change_value(int dir)
{
    if (row == 0) {                                      /* type: 0 = no effect, 1.. = colour / b&w / slide / own */
        int ct = cur_type();
        int nt = clampi(ct + dir, 0, TYPE_COUNT);
        if (nt != ct) {
            remember();
            if (nt == 0) st_film = 0;
            else {
                int g = types[nt - 1].first_group + clampi(memo_group[nt - 1], 0, types[nt - 1].ngroups - 1);
                st_film = 1 + groups[g].first + clampi(memo_film[g], 0, groups[g].n - 1);
            }
            dirty |= D_ROW(0) | D_ROW(1) | D_ROW(2) | D_INFO | film_changed() | D_LISTALL;
        }
    } else if (row == 1) {                               /* brand inside the type */
        if (st_film > 0) {
            int g = films[st_film - 1].group;
            int t = groups[g].type;
            int ng = types[t].first_group + clampi(g - types[t].first_group + dir, 0, types[t].ngroups - 1);
            if (ng != g) {
                remember();
                st_film = 1 + groups[ng].first + clampi(memo_film[ng], 0, groups[ng].n - 1);
                remember();
                dirty |= D_ROW(1) | D_ROW(2) | D_INFO | film_changed();
            }
        }
    } else if (row == 2) {                               /* film inside the brand */
        if (st_film > 0) {
            const group_t *g = &groups[films[st_film - 1].group];
            int pos = clampi(st_film - 1 - g->first + dir, 0, g->n - 1);
            if (1 + g->first + pos != st_film) {
                st_film = 1 + g->first + pos;
                remember();
                dirty |= D_ROW(2) | D_INFO | film_changed();
            }
        }
    } else if (row == 3) {                               /* push / pull: next / previous notch */
        if (n_opts() > 1) {
            const film_t *f = &films[st_film - 1];
            int i = cur_opt();
            int ni = clampi(i + dir, 0, f->np - 1);
            if (ni != i) { st_push = popts[f->pfirst + ni].shift; dirty |= D_ROW(3) | D_INFO; }
        }
    }
    else if (row == JROW) {                              /* JPG with the effect: off / half / full size */
        int nv = clampi(st_jpg + dir, 0, 2);
        if (nv != st_jpg) { st_jpg = nv; dirty |= D_ROW(JROW); }
    }
    else {                                               /* a slider */
        int j = row - 4;
        int *p = sl_var(j);
        int nv = clampi(*p + dir * sl_def[j].step, sl_def[j].lo, sl_def[j].hi);
        if (nv != *p) { *p = nv; dirty |= D_ROW(row); }
    }
}

static void set_row(int r)
{
    r = clampi(r, 0, NROWS - 1);
    if (r != row) {
        dirty |= D_ROW(row) | D_ROW(r);                                /* selection colours change */
        row = r;
        {                                                              /* keep the row inside the window */
            int nt = top, p = lst_pos(r);
            if (r < 3) nt = 0;                                         /* back on the film rows: list from its start */
            else if (p >= 0) {
                if (p < nt) nt = p;
                if (p >= nt + VIS) nt = p - VIS + 1;
            }
            if (nt != top) { top = nt; dirty |= D_LISTALL; }
        }
    }
}

/* rows that can not be changed right now are skipped: brand / film without a film, the rows of the list that the
 * chosen film does not have (nothing at all without a film), push-pull for a film without variants */
static int row_enabled(int r)
{
    if (r == 0) return 1;
    if (r == 1 || r == 2) return st_film > 0;
    if (lst_pos(r) < 0) return 0;
    if (r == 3) return n_opts() > 1;
    return 1;
}

/* joystick up / down */
static void move_row(int dir)
{
    int r = row + dir;
    while (r >= 0 && r < NROWS && !row_enabled(r)) r += dir;
    if (r < 0 || r >= NROWS) return;
    set_row(r);
}

/* ------------------------------------------------------------------ keys */
static int idle_photo_mode(void)                  /* the camera is idle (photo mode or live view): the same condition under
                                                   * which ML itself would open its menu with this key */
{
    return gui_state == GUISTATE_IDLE;
}

static unsigned int filmlook_keypress(unsigned int key)
{
    if (ui_state == UI_CLOSED) {
        /* TRASH opens the panel (in photo mode and in live view) before ML can open its own menu: modules see the key first.
         * ML's own menu stays reachable with a long press of the joystick centre. INFO is Canon's again */
        if (key == MODULE_KEY_TRASH && idle_photo_mode()) { ui_state = UI_OPENING; return 0; }
        return 1;
    }
    if (ui_state != UI_OPEN) return 0;           /* opening / closing: swallow everything */

    klog_add(key);
    last_input_ms = get_ms_clock();
    /* only change values here and let the shoot task draw: the key handler must stay fast */
    switch (key) {
    case MODULE_KEY_WHEEL_DOWN:                              /* rear dial right  */
    case MODULE_KEY_WHEEL_RIGHT:                             /* top dial right   */
    case MODULE_KEY_PRESS_RIGHT: change_value(+1); break;    /* joystick right   */
    case MODULE_KEY_WHEEL_UP:                                /* rear dial left   */
    case MODULE_KEY_WHEEL_LEFT:                              /* top dial left    */
    case MODULE_KEY_PRESS_LEFT:  change_value(-1); break;    /* joystick left    */
    case MODULE_KEY_PRESS_DOWN:  move_row(+1); break;       /* joystick down    */
    case MODULE_KEY_PRESS_UP:    move_row(-1); break;       /* joystick up      */
    case MODULE_KEY_PRESS_SET:
    case MODULE_KEY_JOY_CENTER:  want_save = 1; break;      /* SET = save the settings */
    case MODULE_KEY_TRASH:
    case MODULE_KEY_INFO:
    case MODULE_KEY_MENU:        ui_state = UI_CLOSING; return 0;
    case MODULE_KEY_PRESS_HALFSHUTTER:
    case MODULE_KEY_PLAY:        ui_state = UI_CLOSING; return 1;   /* pass on to the camera */
    default: break;                                                 /* ignore everything else */
    }
    return 0;
}

/* runs in the shoot task, about every 50 ms; opening / closing may sleep here */
static unsigned int filmlook_shoot_task(unsigned int ctx)
{
    if (ui_state == UI_OPENING) do_open();
    else if (ui_state == UI_CLOSING) do_close();
    else if (ui_state == UI_OPEN) {
        static int pal_t = 0;
        int now = get_ms_clock();
        if (now - pal_t > 400) { pal_t = now; pal_apply(); }
        redraw_dirty();
    }
    return 0;
}

/* ------------------------------------------------------------------ photo -> sidecar queue */
#define QMAX 8
typedef struct {
    int used, number, t;
    char dir[40];
    char prefix[8];
    int film;
    int jpg;                     /* JPG z efektem at the time of the photo: 0 off, 1 half size, 2 full size */
    look_t look;
} qitem_t;
static qitem_t queue[QMAX];

static void on_new_photo(int number)
{
    int i;
    if (st_film == 0) { flog("photo, film Off: no sidecar, number", number); return; }
    for (i = 0; i < QMAX; i++) {
        if (!queue[i].used) {
            const char *d = get_dcim_dir();
            const char *p = get_file_prefix();
            int dl = d ? strlen(d) : 0;
            qitem_t *q = &queue[i];
            if (!d || dl <= 0 || dl > 36) { flog("photo: bad DCIM path", number); return; }
            strcpy(q->dir, d);
            if (q->dir[dl - 1] != '/') { q->dir[dl] = '/'; q->dir[dl + 1] = 0; }
            { int k; const char *pp = p ? p : "IMG_"; for (k = 0; k < 4 && pp[k]; k++) q->prefix[k] = pp[k]; q->prefix[k] = 0; }
            q->number = number; q->t = get_ms_clock();
            q->film = eff_film(); cur_look(&q->look); q->jpg = st_jpg;
            q->used = 1;
            flog_s("photo queued", films[q->film - 1].full);
            return;
        }
    }
    flog("photo: queue full", number);
}

PROP_HANDLER(PROP_FILE_NUMBER_A)
{
    on_new_photo(buf[0]);
}


/* ------------------------------------------------------------------ JPG with the film look
 * develop.h does the work (decode the camera's JPEG or the full size preview inside the CR2, apply the film, encode);
 * this part only moves bytes: it reads the source and writes FILMLOOK/IMG_xxxx.JPG at the top of the card, in a
 * low priority task, one photo at a time. Nothing of the camera's own files is changed. */
#define DEV_Q  4
#define IOBUF  (64 * 1024)
typedef struct {
    int used, tries, film, mode, number;
    char dir[40], prefix[8];
    look_t look;
} dev_job_t;
static dev_job_t dev_q[DEV_Q];
static int dev_task_started = 0;

static struct {
    FILE *in, *out;
    uint8_t *ibuf, *obuf;
    uint32_t ibase, ilen;
    uint32_t on;
} dio;

static int dev_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    (void)ctx;
    while (n) {
        uint32_t c;
        if (off < dio.ibase || off >= dio.ibase + dio.ilen) {          /* the window of 64 KB does not cover it: refill */
            int got;
            if (FIO_SeekSkipFile(dio.in, off, SEEK_SET) < 0) return 1;
            got = FIO_ReadFile(dio.in, dio.ibuf, IOBUF);
            if (got <= 0) { dio.ilen = 0; return 1; }
            dio.ibase = off; dio.ilen = (uint32_t)got;
        }
        c = dio.ibase + dio.ilen - off;
        if (c > n) c = n;
        memcpy(d, dio.ibuf + (off - dio.ibase), c);
        d += c; off += c; n -= c;
    }
    return 0;
}

static int dev_flush(void)
{
    int r = 0;
    if (dio.on) { r = FIO_WriteFile(dio.out, dio.obuf, dio.on) == (int)dio.on ? 0 : 1; dio.on = 0; }
    return r;
}

static int dev_write(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    while (n) {
        size_t c = IOBUF - dio.on;
        if (c > n) c = n;
        memcpy(dio.obuf + dio.on, d, c);
        dio.on += (uint32_t)c; d += c; n -= c;
        if (dio.on == IOBUF && dev_flush()) return 1;
    }
    return 0;
}

/* 0 = done (or hopeless), 1 = try again later (no memory right now) */
static int dev_run(const dev_job_t *j)
{
    char src[96], dst[64];
    uint32_t size = 0;
    const film_t *f = &films[j->film - 1];
    int scale = (j->mode == 2) ? 1 : 2;
    size_t need = (j->mode == 2) ? fl_mem_needed(5616, 1) : fl_mem_needed(5616, 2);
    uint8_t *mem;
    fl_io io;
    fl_result res;
    int t0 = get_ms_clock(), root = 0, i;

    snprintf(src, sizeof(src), "%s%s%04d.JPG", j->dir, j->prefix, j->number);        /* the camera's JPG, or ... */
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000)
        snprintf(src, sizeof(src), "%s%s%04d.CR2", j->dir, j->prefix, j->number);   /* ... the preview inside the RAW */
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000) { flog_s("JPG: no source file", src); return 0; }
    for (i = 0; j->dir[i] && j->dir[i] != '/'; i++) {}
    root = j->dir[i] == '/' ? i + 1 : 0;                                              /* "B:/" */
    { char r[8]; int k; for (k = 0; k < root && k < 7; k++) r[k] = j->dir[k]; r[k] = 0;
      snprintf(dst, sizeof(dst), "%sFILMLOOK/%s%04d.JPG", r, j->prefix, j->number); }

    mem = (uint8_t *)malloc(need);
    dio.ibuf = (uint8_t *)fio_malloc(IOBUF);
    dio.obuf = (uint8_t *)fio_malloc(IOBUF);
    if (!mem || !dio.ibuf || !dio.obuf) {
        if (mem) free(mem);
        if (dio.ibuf) fio_free(dio.ibuf);
        if (dio.obuf) fio_free(dio.obuf);
        flog("JPG: no memory now, KB needed", (int)(need >> 10));
        return 1;
    }
    dio.in = FIO_OpenFile(src, O_RDONLY | O_SYNC);
    dio.out = dio.in ? FIO_CreateFile(dst) : 0;
    dio.ibase = 0; dio.ilen = 0; dio.on = 0;
    if (!dio.in || !dio.out) {
        flog_s("JPG: cannot open", dio.in ? dst : src);
        if (dio.in) FIO_CloseFile(dio.in);
        if (dio.out) FIO_CloseFile(dio.out);
    } else {
        io.read = dev_read; io.rctx = 0; io.src_size = size;
        io.write = dev_write; io.wctx = 0;
        io.mem = mem; io.mem_size = need;
        fl_develop(&io, f, &j->look, scale, 90, &res);
        if (res.rc == 0 && dev_flush() != 0) res.rc = SE_E_IO;
        FIO_CloseFile(dio.in);
        FIO_CloseFile(dio.out);
        if (res.rc == 0) {
            flog_s("JPG written", dst);
            flog("JPG width", res.out_w);
            flog("JPG ms", get_ms_clock() - t0);
            flog("JPG arena peak KB", (int)(res.mem_peak >> 10));
        } else {
            flog_s("JPG failed, source", src);
            flog("JPG error code", res.rc);
            FIO_RemoveFile(dst);
        }
    }
    free(mem);
    fio_free(dio.ibuf);
    fio_free(dio.obuf);
    return 0;
}

static void dev_task(void *arg)
{
    (void)arg;
    for (;;) {
        int i;
        msleep(1000);
        for (i = 0; i < DEV_Q; i++) {
            dev_job_t *j = &dev_q[i];
            if (!j->used) continue;
            powersave_prohibit();                       /* the camera must not switch itself off in the middle of a photo */
            if (dev_run(j) == 0 || ++j->tries >= 12) j->used = 0;
            else msleep(5000);
            powersave_permit();
        }
    }
}

static void dev_enqueue(const qitem_t *q)
{
    int i;
    if (q->jpg <= 0) return;
    for (i = 0; i < DEV_Q; i++) {
        if (!dev_q[i].used) {
            dev_job_t *j = &dev_q[i];
            memset(j, 0, sizeof(*j));
            strcpy(j->dir, q->dir); strcpy(j->prefix, q->prefix);
            j->film = q->film; j->mode = q->jpg; j->number = q->number; j->look = q->look;
            j->used = 1;
            if (!dev_task_started) { dev_task_started = 1; task_create("fl_dev", 0x1c, 0x6000, dev_task, 0); }
            flog_s("JPG queued", films[q->film - 1].full);
            return;
        }
    }
    flog("JPG: queue full", q->number);
}

static void write_sidecar(qitem_t *q)
{
    static char xmp[6144];
    char path[80];
    int n;
    snprintf(path, sizeof(path), "%s%s%04d.xmp", q->dir, q->prefix, q->number);
    n = build_xmp(xmp, sizeof(xmp), &films[q->film - 1], &q->look);
    if (n > 0) {
        FILE *f = FIO_CreateFile(path);
        if (f) { FIO_WriteFile(f, xmp, n); FIO_CloseFile(f); flog_s("XMP written", path); }
        else flog_s("XMP cannot create", path);
    } else flog("XMP build error", n);
    dev_enqueue(q);
    q->used = 0;
}

static unsigned int filmlook_seconds(unsigned int ctx)
{
    int i, t = get_ms_clock();
    kill_ml_overlay();
    if (ui_state == UI_OPEN && (t - last_input_ms) > 45000) { flog("auto-close (idle)", 0); ui_state = UI_CLOSING; }
    for (i = 0; i < QMAX; i++)
        if (queue[i].used && (t - queue[i].t) >= 3000)       /* let the camera finish writing the photo */
            write_sidecar(&queue[i]);
    return 0;
}

/* ------------------------------------------------------------------ module glue */
static unsigned int filmlook_init(void)
{
    memo_init();
    sets_init();
    load_state();
    kill_ml_overlay();
    flog_s("FILMLOOK module loaded", VERSION);
    pal_build();
    flog_s("film", st_film > 0 ? films[st_film - 1].full : "Off");
    return 0;
}

static unsigned int filmlook_deinit(void) { return 0; }

MODULE_INFO_START()
    MODULE_INIT(filmlook_init)
    MODULE_DEINIT(filmlook_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS, filmlook_keypress, 0)
    MODULE_CBR(CBR_SHOOT_TASK, filmlook_shoot_task, 0)
    MODULE_CBR(CBR_SECONDS_CLOCK, filmlook_seconds, 0)
MODULE_CBRS_END()

MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_FILE_NUMBER_A)
MODULE_PROPHANDLERS_END()
