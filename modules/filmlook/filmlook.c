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

#define VERSION "v0.7"
#define LOG_PATH      "ML/LOGS/FILMLOOK.LOG"
#define SETTINGS_PATH "ML/SETTINGS/FILMLOOK.TXT"
extern int gui_state;
extern int lv;
extern int gui_menu_shown(void);
extern int ml_shutdown_requested;
extern int set_config_var(const char *name, int new_value);
extern int get_config_var(const char *name);

#include "state.h"

static void cur_look(look_t *L)
{
    L->strength = st_strength; L->grain = st_grain; L->contrast = st_contrast; L->fade = st_fade; L->warm = st_warm;
    L->shadow = st_shadow; L->high = st_high; L->sat = st_sat; L->vig = st_vig; L->hal = st_hal;
}

/* ------------------------------------------------------------------ log */
/* several tasks write to the log (key task, shoot task, develop task): one at a time, so lines never get mixed */
static struct semaphore *log_sem = 0;

static void log_lock(void)   { if (log_sem) take_semaphore(log_sem, 2000); }
static void log_unlock(void) { if (log_sem) give_semaphore(log_sem); }

static void flog(const char *a, int v)
{
    char line[128];
    FILE *f;
    snprintf(line, sizeof(line), "%d  %s %d\n", get_ms_clock(), a, v);
    log_lock();
    f = FIO_CreateFileOrAppend(LOG_PATH);
    if (f) { FIO_WriteFile(f, line, strlen(line)); FIO_CloseFile(f); }
    log_unlock();
}

static void flog_s(const char *a, const char *s)
{
    char line[160];
    FILE *f;
    snprintf(line, sizeof(line), "%d  %s %s\n", get_ms_clock(), a, s);
    log_lock();
    f = FIO_CreateFileOrAppend(LOG_PATH);
    if (f) { FIO_WriteFile(f, line, strlen(line)); FIO_CloseFile(f); }
    log_unlock();
}

/* key events and diagnostic events of the screen are collected in RAM and written to the log when the panel closes,
 * so that the (slow) card access never delays the camera's button handling */
#define KLOG_N 96
static int klog_t[KLOG_N];
static int klog_k[KLOG_N];
static int klog_n = 0;

static void klog_add(int key)
{
    if (klog_n < KLOG_N) { klog_t[klog_n] = get_ms_clock(); klog_k[klog_n] = key; klog_n++; }
}

#define DLOG_N 40
static int dlog_t[DLOG_N], dlog_a[DLOG_N], dlog_b[DLOG_N], dlog_c[DLOG_N];
static const char *dlog_s[DLOG_N];
static volatile int dlog_n = 0;

/* tag is a string literal; a, b, c are numbers worth knowing (gui_state, GUI mode, live view ...) */
static void dlog_add(const char *tag, int a, int b, int c)
{
    int i = dlog_n;
    if (i < DLOG_N) {
        dlog_t[i] = get_ms_clock(); dlog_s[i] = tag; dlog_a[i] = a; dlog_b[i] = b; dlog_c[i] = c;
        dlog_n = i + 1;
    }
}

static void klog_flush(void)
{
    static char buf[KLOG_N * 24 + DLOG_N * 64 + 64];
    int i, n = 0, m = dlog_n;
    if (klog_n <= 0 && m <= 0) return;
    if (m > DLOG_N) m = DLOG_N;
    for (i = 0; i < m; i++)
        n += snprintf(buf + n, sizeof(buf) - n, "%d  screen: %s %d %d %d\n", dlog_t[i], dlog_s[i], dlog_a[i], dlog_b[i], dlog_c[i]);
    dlog_n = 0;
    if (klog_n > 0) {
        n += snprintf(buf + n, sizeof(buf) - n, "%d  keys while panel open (time_ms key):\n", get_ms_clock());
        for (i = 0; i < klog_n && n < (int)sizeof(buf) - 24; i++)
            n += snprintf(buf + n, sizeof(buf) - n, "  %d %d\n", klog_t[i], klog_k[i]);
        klog_n = 0;
    }
    {
        FILE *f;
        log_lock();
        f = FIO_CreateFileOrAppend(LOG_PATH);
        if (f) { FIO_WriteFile(f, buf, n); FIO_CloseFile(f); }
        log_unlock();
    }
}

/* ------------------------------------------------------------------ settings file */
#include "persist.h"

static void save_state(void)
{
    int n = cfg_build();
    FILE *f = FIO_CreateFile(SETTINGS_PATH);
    if (f) { FIO_WriteFile(f, cfg_buf, n); FIO_CloseFile(f); }
    else flog("save_state: cannot create file", 0);
}

static void load_state(void)
{
    int n = read_file(SETTINGS_PATH, cfg_buf, sizeof(cfg_buf) - 1);
    cfg_parse(n);
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

/* the panel's 16 colours are written whether the bitmap layer is muted or not: while the screen is built up, every other
 * palette entry stays transparent, so nothing of Canon's or ML's own screens can show through */
static void pal_apply(void)
{
    int i, opacity = (LCD_Palette[3 * COLOR_BLACK + 2] >> 24) & 0xFF;
    if (opacity == 0) opacity = 3;                      /* fully opaque */
    for (i = 0; i < PAL_COUNT; i++) {
        int e = pal_entry(pal_rgb[i], opacity);
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i)], e);
        EngDrvOut(LCD_Palette[3 * (PAL_BASE + i) + 0x300], e);
    }
}

/* every palette entry transparent: the same thing bmp_off() does, repeated while Canon is changing its screen,
 * because Canon loads its own palette again when it switches the mode and would show its menu for a moment */
static void pal_mute_all(void)
{
    int i;
    for (i = 0; i < 0x100; i++) {
        EngDrvOut(LCD_Palette[3 * i], 0x00FF0000);
        EngDrvOut(LCD_Palette[3 * i + 0x300], 0x00FF0000);
    }
}

/* what has to be redrawn (set from the key handler, handled in the shoot task) */
#define D_ROW(r) (1 << (r))          /* rows 0..15 */
#define D_INFO   (1 << 16)           /* kind / description block under the film row */
#define D_BAR    (1 << 17)           /* scroll bar of the list */
#define D_LISTALL (1 << 18)          /* the whole list: it scrolled, or the rows / the sliders changed with the film */
#define D_STATUS (1 << 19)           /* the JPG status at the top left */
static volatile int dirty = 0;
static const char *msg_text = "Zapisano";
static int dev_pending(void);
static volatile int dev_last_ms;

/* the drawing functions write straight into the screen buffer of the camera (call with BMP_LOCK held) */
static void ui_attach(void)
{
    ui_fb = bmp_vram();
    ui_pitch = BMPPITCH;
}

static int msg_until = 0;            /* ms clock: show the message in the hint line until then */
static volatile int want_save = 0;
static volatile int want_reset = 0;  /* SET on the row "Ustawienia domyślne" */

/* the JPG status at the top left: the number of JPGs that wait or are made now, otherwise how long the last one took */
static char st_last[32] = "?";                  /* what the status field shows now ('?' = not drawn yet) */

static void status_text(char *t, int n)
{
    int pend = dev_pending(), ms = dev_last_ms;
    if (pend > 0)      snprintf(t, n, "kolejka JPG: %d", pend);
    else if (ms > 0)   snprintf(t, n, "obróbka: %d,%d s", ms / 1000, (ms % 1000) / 100);
    else               t[0] = 0;
}

static void redraw_dirty(void)
{
    int d = dirty;
    if (want_reset) {
        int k;
        want_reset = 0;
        k = reset_film();
        dirty |= D_LISTALL;
        d |= D_LISTALL;
        want_save = 1;
        flog("defaults restored, sliders changed", k);
        if (k == 0) msg_text = "Ustawienia są już domyślne";
        else        msg_text = "Przywrócono ustawienia domyślne filmu";
    }
    if (want_save) {
        want_save = 0;
        save_state();
        msg_until = get_ms_clock() + 1500;
        BMP_LOCK ( ui_attach(); draw_hint(msg_text); )
        msg_text = "Zapisano";
        flog_s("settings saved, film", st_film > 0 ? films[eff_film() - 1].full : "Off");
    } else if (msg_until && get_ms_clock() > msg_until) {
        msg_until = 0;
        BMP_LOCK ( ui_attach(); draw_hint(0); )
    }
    {                                                   /* the JPG status changes by itself (in the background) */
        char t[32];
        status_text(t, sizeof(t));
        if (strcmp(t, st_last) != 0) { strcpy(st_last, t); d |= D_STATUS; dirty |= D_STATUS; }
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
        if (d & D_STATUS)  draw_status(st_last);
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
 * The Canon menu that appears for a moment is kept out of sight: the whole palette is muted and muted again while
 * Canon changes its screen, the panel is drawn into the buffer meanwhile and only then its own 16 colours are
 * switched on (nothing else ever becomes visible, so there is no flash of Canon's or ML's menu). */
static void do_open(void)
{
    int i, t_mode = 0, t0 = get_ms_clock();
    kill_ml_overlay();
    row = (st_film > 0) ? 2 : 0;                        /* start on the film row when a film is chosen */
    top = 0;
    list_build();
    klog_n = 0;
    msg_until = 0;
    want_save = 0;
    want_reset = 0;
    msg_text = "Zapisano";
    strcpy(st_last, "?");
    last_input_ms = get_ms_clock();
    dlog_add("open start: GUI mode, live view, ML menu", CUR_GUI_MODE, lv, gui_menu_shown());
    bmp_off();
    pal_mute_all();
    if (!lv && CUR_GUI_MODE != 2) {
        SetGUIRequestMode(2);
        for (i = 0; i < 150; i++) {                    /* up to 1.5 s: until the mode is there, then Canon draws its menu */
            pal_mute_all();
            if (gui_menu_shown()) dlog_add("ML MENU IS SHOWN while opening", i, CUR_GUI_MODE, 0);
            if (CUR_GUI_MODE == 2) {
                if (!t_mode) { t_mode = get_ms_clock(); dlog_add("GUI mode 2 reached after ms", t_mode - t0, 0, 0); }
                else if (get_ms_clock() - t_mode >= 300) break;
            }
            msleep(10);
        }
    }
    pal_mute_all();
    canon_gui_disable_front_buffer();
    paint_all();
    pal_apply();                                       /* the panel's colours: the first thing that becomes visible */
    msleep(60);
    canon_gui_disable_front_buffer();
    paint_all();
    pal_apply();
    last_input_ms = get_ms_clock();
    ui_state = UI_OPEN;
    dlog_add("open done after ms, GUI mode, ML menu", get_ms_clock() - t0, CUR_GUI_MODE, gui_menu_shown());
    flog("screen opened, film", st_film);
}

static void do_close(void)
{
    dirty = 0;
    want_save = 0;
    save_state();
    bmp_off();                                          /* hides the panel at once (also marks the layer as muted for bmp_on) */
    pal_mute_all();
    if (CUR_GUI_MODE != 0) {
        SetGUIRequestMode(0);
        for (int i = 0; i < 20; i++) { pal_mute_all(); msleep(10); }
    }
    canon_gui_enable_front_buffer(1);
    msleep(150);
    bmp_on();                                           /* Canon's palette is back */
    dlog_add("closed, GUI mode, ML menu", CUR_GUI_MODE, lv, gui_menu_shown());
    klog_flush();
    flog("screen closed, film", st_film);
    ui_state = UI_CLOSED;
}

/* the chosen film changed: keep the choice, swap the sliders when it moves to / from an own preset, rebuild the list.
 * Returns the dirty bits of the list */
static int film_changed(void)
{
    int old_n = lst_n, swapped;
    st_push = 0;
    swapped = switch_set();                             /* every film has its own sliders */
    list_build();
    if (swapped || old_n != lst_n) { top = 0; return D_LISTALL; }
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
    else if (row == RROW) {                              /* the dial does nothing here: SET restores the defaults */
    }
    else if (row == JROW) {                              /* JPG with the effect: off / half / full size */
        int nv = clampi(st_jpg + dir, 0, 2);
        if (nv != st_jpg) { st_jpg = nv; dirty |= D_ROW(JROW); }
    }
    else {                                               /* a slider */
        int j = row - 4;
        int *p = sl_var(j);
        int nv = clampi(*p + dir * sl_def[j].step, sl_def[j].lo, sl_def[j].hi);
        if (nv != *p) { *p = nv; dirty |= D_ROW(row) | D_ROW(RROW); }
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
        if (key == MODULE_KEY_TRASH && idle_photo_mode()) {
            dlog_add("KOSZ: the panel opens; gui_state, live view, GUI mode", gui_state, lv, CUR_GUI_MODE);
            ui_state = UI_OPENING;
            return 0;
        }
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
    case MODULE_KEY_JOY_CENTER:                             /* SET = save the settings; on the last row: back to the defaults */
        if (row == RROW) want_reset = 1; else want_save = 1;
        break;
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

static volatile int last_shot_ms;                /* when the last photo was taken (see the develop part) */

static void on_new_photo(int number)
{
    int i;
    last_shot_ms = get_ms_clock();
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
 * this part only moves bytes. It is built so that a card pulled out of the camera can not leave a damaged file:
 *   - the camera's own files are only ever READ, never changed or renamed;
 *   - nothing is developed while the camera is still writing a photo (and the job waits whenever a new photo is being
 *     written), the source JPEG must be complete (ends with its end marker, a cut one is refused by the decoder);
 *   - the finished JPG is first built in RAM and written to FILMLOOK/ in one short go (a few tenths of a second). The long
 *     part (decoding, the film look, encoding) does not touch the card at all. When the picture does not fit in RAM it
 *     is written while it is made, as before;
 *   - while FILMLOOK/<name>.JPG is being written the file FILMLOOK/WRITING.TMP holds its name. When the camera starts
 *     and finds that note, the file it names is checked (a JPEG that does not end properly is deleted) and the note removed;
 *   - when the camera shuts down or the card cover opens, the job stops at once and removes what it started. */
#define DEV_Q      4
#define IOBUF      (64 * 1024)
#define OUT_CHUNK  (256 * 1024)
#define MARK_NAME  "FILMLOOK/WRITING.TMP"
typedef struct {
    int used, tries, film, mode, number, t_shot;
    char dir[40], prefix[8];
    look_t look;
} dev_job_t;
static dev_job_t dev_q[DEV_Q];
static int dev_task_started = 0;
static volatile int dev_busy = 0;            /* a job is running right now */
static volatile int dev_last_ms = 0;         /* how long the last finished JPG took (ms), 0 = none yet */
static volatile int dev_done_n = 0;          /* finished JPGs since the camera was switched on */

static volatile int cam_job = 0;             /* PROP_LAST_JOB_STATE: 0 = the camera is idle, 8 = writing to the card, B = busy */
static volatile int last_shot_ms = -100000;  /* when the last photo was taken */
static volatile int cover_base = -1;         /* PROP_CARD_COVER as it was when the camera started (cover closed) ... */
static volatile int cover_now = -1;          /* ... and now: a different value means the card cover is open */

PROP_HANDLER(PROP_LAST_JOB_STATE) { cam_job = buf[0]; }
PROP_HANDLER(PROP_CARD_COVER)
{
    cover_now = buf[0];
    if (cover_base < 0) cover_base = buf[0];
    else if (buf[0] != cover_base) dlog_add("card cover property changed to", buf[0], cover_base, 0);
}

/* the job may go on: the camera is not shutting down and the card cover is closed */
static int dev_alive(void)
{
    return !ml_shutdown_requested && (cover_base < 0 || cover_now == cover_base);
}

/* wait while the camera writes a photo (at most gate_left ms for one job, so a property that stays busy can not stop
 * the development for ever); 1 = stop this job (shutdown / card cover) */
static int gate_left = 0;

static int dev_gate(void)
{
    while ((cam_job || get_ms_clock() - last_shot_ms < 2500) && dev_alive() && gate_left > 0) { msleep(50); gate_left -= 50; }
    return dev_alive() ? 0 : 1;
}

static struct {
    FILE *in, *out;
    uint8_t *ibuf, *obuf, *big;
    uint32_t ibase, ilen;
    uint32_t on;                             /* bytes waiting in obuf (streaming) */
    uint32_t cap, len, total;                /* the RAM copy of the JPG: size, used, all bytes made so far */
    int stream;                              /* the file is open and being written */
    char dst[64], mark[64];
} dio;

static int dev_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    (void)ctx;
    while (n) {
        uint32_t c;
        if (off < dio.ibase || off >= dio.ibase + dio.ilen) {          /* the window of 64 KB does not cover it: refill */
            int got;
            if (dev_gate()) return 1;
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
    if (dio.on) {
        if (!dev_alive()) return 1;
        r = FIO_WriteFile(dio.out, dio.obuf, dio.on) == (int)dio.on ? 0 : 1;
        dio.on = 0;
    }
    return r;
}

/* the file is created (with the note that names it) and what is in RAM goes to the card */
static int dev_open_out(void)
{
    uint32_t o = 0;
    FILE *m;
    if (dev_gate()) return 1;
    m = FIO_CreateFile(dio.mark);
    if (!m) { flog_s("JPG: cannot make the note", dio.mark); return 1; }
    FIO_WriteFile(m, dio.dst, strlen(dio.dst));
    FIO_CloseFile(m);
    dio.out = FIO_CreateFile(dio.dst);
    if (!dio.out) { flog_s("JPG: cannot create", dio.dst); return 1; }
    dio.stream = 1;
    while (o < dio.len) {
        uint32_t c = dio.len - o;
        if (c > OUT_CHUNK) c = OUT_CHUNK;
        if (!dev_alive()) return 1;
        if (FIO_WriteFile(dio.out, dio.big + o, c) != (int)c) { flog_s("JPG: card write failed", dio.dst); return 1; }
        o += c;
    }
    dio.len = 0;
    return 0;
}

static int dev_write(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    dio.total += (uint32_t)n;
    if (!dio.stream) {
        if (dio.big && dio.len + n <= dio.cap) { memcpy(dio.big + dio.len, d, n); dio.len += (uint32_t)n; return 0; }
        if (dev_open_out()) return 1;                    /* RAM is full (or there is none): go on writing to the card */
    }
    while (n) {
        size_t c = IOBUF - dio.on;
        if (c > n) c = n;
        memcpy(dio.obuf + dio.on, d, c);
        dio.on += (uint32_t)c; d += c; n -= c;
        if (dio.on == IOBUF && dev_flush()) return 1;
    }
    return 0;
}

/* a JPEG file is complete when it ends with FF D9 */
static int jpg_complete(const char *path)
{
    uint32_t size = 0;
    uint8_t t[2] = { 0, 0 };
    FILE *f;
    int ok = 0;
    if (FIO_GetFileSize(path, &size) != 0 || size < 2000) return 0;
    f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (!f) return 0;
    if (FIO_SeekSkipFile(f, size - 2, SEEK_SET) >= 0 && FIO_ReadFile(f, t, 2) == 2 && t[0] == 0xFF && t[1] == 0xD9) ok = 1;
    FIO_CloseFile(f);
    return ok;
}

/* at start: a note left behind means a write was interrupted (power lost, battery out): look at the file it names */
static void dev_recover(void)
{
    static char buf[80];
    int n = read_file(MARK_NAME, buf, sizeof(buf) - 1);
    if (n < 0) return;                                   /* no note: nothing was interrupted */
    buf[n > 0 ? n : 0] = 0;
    if (n > 0 && !jpg_complete(buf)) { FIO_RemoveFile(buf); flog_s("JPG: removed an unfinished file", buf); }
    else if (n > 0) flog_s("JPG: the file named in the note is complete, kept", buf);
    FIO_RemoveFile(MARK_NAME);
}

/* 0 = done (or hopeless), 1 = try again later (no memory right now, the source is not complete yet, the camera is busy) */
static int dev_run(const dev_job_t *j)
{
    char src[96], r[8];
    uint32_t size = 0;
    const film_t *f = &films[j->film - 1];
    int scale = (j->mode == 2) ? 1 : 2;
    size_t need = (j->mode == 2) ? fl_mem_needed(5616, 1) : fl_mem_needed(5616, 2);
    uint8_t *mem;
    fl_io io;
    fl_result res;
    int t0 = 0, t_proc = 0, ok = 0, retry = 0, i, root;

    snprintf(src, sizeof(src), "%s%s%04d.JPG", j->dir, j->prefix, j->number);        /* the camera's JPG, or ... */
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000)
        snprintf(src, sizeof(src), "%s%s%04d.CR2", j->dir, j->prefix, j->number);   /* ... the preview inside the RAW */
    else if (!jpg_complete(src)) { flog_s("JPG: the camera has not finished writing", src); return 1; }
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000) { flog_s("JPG: no source file", src); return 0; }
    for (i = 0; j->dir[i] && j->dir[i] != '/'; i++) {}
    root = j->dir[i] == '/' ? i + 1 : 0;                                              /* "B:/" */
    for (i = 0; i < root && i < 7; i++) r[i] = j->dir[i];
    r[i] = 0;
    snprintf(dio.dst, sizeof(dio.dst), "%sFILMLOOK/%s%04d.JPG", r, j->prefix, j->number);
    snprintf(dio.mark, sizeof(dio.mark), "%s%s", r, MARK_NAME);

    mem = (uint8_t *)malloc(need);
    dio.ibuf = (uint8_t *)fio_malloc(IOBUF);
    dio.obuf = (uint8_t *)fio_malloc(IOBUF);
    dio.big = 0; dio.cap = 0;
    if (!mem || !dio.ibuf || !dio.obuf) {
        if (mem) free(mem);
        if (dio.ibuf) fio_free(dio.ibuf);
        if (dio.obuf) fio_free(dio.obuf);
        flog("JPG: no memory now, KB needed", (int)(need >> 10));
        return 1;
    }
    { uint32_t c;                                        /* room for the whole finished JPG, as much as there is */
      for (c = (j->mode == 2) ? 12u << 20 : 4u << 20; c >= (1u << 20) && !dio.big; c >>= 1)
          if ((dio.big = (uint8_t *)fio_malloc(c)) != 0) dio.cap = c; }
    dio.in = FIO_OpenFile(src, O_RDONLY | O_SYNC);
    dio.out = 0; dio.stream = 0;
    dio.ibase = 0; dio.ilen = 0; dio.on = 0; dio.len = 0; dio.total = 0;
    if (!dio.in) {
        flog_s("JPG: cannot open", src);
    } else {
        t0 = get_ms_clock();
        io.read = dev_read; io.rctx = 0; io.src_size = size;
        io.write = dev_write; io.wctx = 0;
        io.mem = mem; io.mem_size = need;
        fl_develop(&io, f, &j->look, scale, 90, &res);
        t_proc = get_ms_clock() - t0;
        FIO_CloseFile(dio.in);
        if (res.rc == 0 && !dev_alive()) res.rc = SE_E_IO;
        if (res.rc == 0) {                               /* the short part that writes: the rest of the picture, then close */
            if (!dio.stream && dev_open_out()) res.rc = SE_E_IO;
            else if (dio.stream && dev_flush()) res.rc = SE_E_IO;
        }
        if (dio.out) FIO_CloseFile(dio.out);
        if (res.rc == 0) {                               /* check what is on the card, then take the note away */
            uint32_t got = 0;
            if (FIO_GetFileSize(dio.dst, &got) == 0 && got == dio.total && jpg_complete(dio.dst)) ok = 1;
            else { flog_s("JPG: the file on the card does not match", dio.dst); res.rc = SE_E_IO; }
        }
        if (ok) {
            FIO_RemoveFile(dio.mark);
            dev_last_ms = get_ms_clock() - t0; dev_done_n++;
            flog_s("JPG written", dio.dst);
            flog("JPG width", res.out_w);
            flog("JPG ms processing (RAM only)", t_proc);
            flog("JPG ms all, with the writing", dev_last_ms);
            flog("JPG ms since the shot", get_ms_clock() - j->t_shot);
            flog("JPG bytes", (int)dio.total);
            flog("JPG arena peak KB", (int)(res.mem_peak >> 10));
            flog("JPG made in RAM (1) or on the card (0)", dio.big ? (dio.cap >= dio.total) : 0);
        } else {
            if (dio.stream) { FIO_RemoveFile(dio.dst); FIO_RemoveFile(dio.mark); }
            if (res.rc == SE_E_TRUNCATED) { flog_s("JPG: the source is cut (still being written?), later", src); retry = 1; }
            else { flog_s("JPG failed, source", src); flog("JPG error code", res.rc); retry = 0; }
        }
    }
    if (dio.big) fio_free(dio.big);
    free(mem);
    fio_free(dio.ibuf);
    fio_free(dio.obuf);
    return retry;
}

static void dev_task(void *arg)
{
    (void)arg;
    for (;;) {
        int i;
        msleep(1000);
        for (i = 0; i < DEV_Q; i++) {
            dev_job_t *j = &dev_q[i];
            int r;
            if (!j->used) continue;
            if (!dev_alive()) break;                              /* shutting down / card cover open: nothing is started */
            gate_left = 120000;
            if (dev_gate()) break;
            powersave_prohibit();                                 /* the camera must not switch itself off in the middle of a photo */
            dev_busy = 1;
            r = dev_run(j);
            dev_busy = 0;
            powersave_permit();
            if (r == 0) j->used = 0;
            else if (++j->tries >= 12) { flog("JPG: giving up, number", j->number); j->used = 0; }
            else msleep(5000);
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
            j->film = q->film; j->mode = q->jpg; j->number = q->number; j->look = q->look; j->t_shot = q->t;
            j->used = 1;
            if (!dev_task_started) { dev_task_started = 1; task_create("fl_dev", 0x1c, 0x6000, dev_task, 0); }
            flog_s("JPG queued", films[q->film - 1].full);
            return;
        }
    }
    flog("JPG: queue full", q->number);
}

/* JPGs waiting or being made (for the panel) */
static int dev_pending(void)
{
    int i, n = 0;
    for (i = 0; i < DEV_Q; i++) if (dev_q[i].used) n++;
    for (i = 0; i < QMAX; i++) if (queue[i].used && queue[i].jpg > 0) n++;
    return n;
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
    log_sem = create_named_semaphore("filmlook_log", 1);
    memo_init();
    sets_init();
    load_state();
    kill_ml_overlay();
    flog_s("FILMLOOK module loaded", VERSION);
    pal_build();
    flog_s("film", st_film > 0 ? films[st_film - 1].full : "Off");
    dev_recover();                                      /* a JPG whose writing was interrupted last time */
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
    MODULE_PROPHANDLER(PROP_LAST_JOB_STATE)
    MODULE_PROPHANDLER(PROP_CARD_COVER)
MODULE_PROPHANDLERS_END()
