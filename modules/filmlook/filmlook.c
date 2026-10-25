/*
 * filmlook.c - film look simulation for Canon EOS 5D Mark II (Magic Lantern module)
 *
 *  TRASH opens ONE full-screen panel: type (colour / black & white / slide) -> film + settings (the maker is shown above the film name)
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

#define VERSION "v0.10"
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
    L->shadow = st_shadow; L->high = st_high; L->sat = st_sat; L->vig = st_vig;
    L->hal = 0;                    /* halation is switched off completely (v0.10): the slider is hidden, so an old saved value must not act */
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

/* reads a small text file into dst (at most max bytes), returns the byte count or -1 when there is no file.
 * Magic Lantern refuses (ASSERT, with a red console on the screen) to read more than 8 KB straight into a normal
 * variable, so the data goes through a buffer from fio_malloc first - the way the engine reads its JPGs. */
static int read_text(const char *path, char *dst, int max)
{
    uint32_t size = 0;
    uint8_t *tmp;
    FILE *f;
    int n = 0;
    if (FIO_GetFileSize(path, &size) != 0) return -1;
    if (size > (uint32_t)max) size = (uint32_t)max;
    if (size == 0) return 0;
    tmp = (uint8_t *)fio_malloc(size);
    if (!tmp) return -1;
    f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (f) {
        n = FIO_ReadFile(f, tmp, size);
        FIO_CloseFile(f);
        if (n > (int)size) n = (int)size;
        if (n > 0) memcpy(dst, tmp, (size_t)n);
    } else n = -1;
    fio_free(tmp);
    return n;
}

static void load_state(void)
{
    int n = read_text(SETTINGS_PATH, cfg_buf, sizeof(cfg_buf) - 1);
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
#define D_ROW(r) (1 << (r))          /* rows 0..19 */
#define D_INFO   (1 << 20)           /* kind / description block under the film row */
#define D_BAR    (1 << 21)           /* scroll bar of the list */
#define D_LISTALL (1 << 22)          /* the whole list: it scrolled, or the rows / the sliders changed with the film */
#define D_STATUS (1 << 23)           /* the JPG status at the top left */
#define D_PROG   (1 << 24)           /* the progress line under the title */
#define D_FULL   (1 << 25)           /* the look or the language of the panel changed: new palette / new texts, everything is drawn again */
static volatile int dirty = 0;
static int msg_id = S_MSG_SAVED;     /* the text (i18n.h) that the next save shows in the hint line */
static int dev_pending(void);
static volatile int dev_last_ms;
static volatile int dev_phase;                   /* 0 nothing, 1 waits for the camera, 2 makes the JPG, 3 writes it to the card */
static int dev_prog_now(void);                   /* progress line, 0..1000 */

/* the drawing functions write straight into the screen buffer of the camera (call with BMP_LOCK held) */
static void ui_attach(void)
{
    ui_fb = bmp_vram();
    ui_pitch = BMPPITCH;
}

static int msg_until = 0;            /* ms clock: show the message in the hint line until then */
static volatile int want_save = 0;
static volatile int want_reset = 0;  /* SET on the row "Domyślne tego filmu" */
static volatile int want_reset_all = 0;   /* the second SET on the row "Domyślne wszystkich filmów" */
static volatile int arm_new = 0;     /* the first SET on that row: the hint line asks for the second one */
static int arm_t = 0;                /* ms clock of the first SET */

/* the JPG status at the top left: the number of JPGs that wait or are made now, otherwise how long the last one took */
static char st_last[32] = "?";                  /* what the status field shows now ('?' = not drawn yet) */

static int prog_shown = -1;                      /* what the progress line shows now (-1 = not drawn yet) */

static void status_text(char *t, int n)
{
    int pend = dev_pending(), ms = dev_last_ms, ph = dev_phase;
    if (pend > 0 && ph == 1)  snprintf(t, n, "%s", TR(S_ST_WAIT));
    else if (pend > 0 && ph == 3) snprintf(t, n, "%s", TR(S_ST_WRITE));
    else if (pend > 0) snprintf(t, n, TR(S_ST_QUEUE), pend);
    else if (ms > 0)   snprintf(t, n, TR(S_ST_BUSY), ms / 1000, (ms % 1000) / 100);
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
        msg_id = k == 0 ? S_MSG_ALRDY : S_MSG_RSTF;
    }
    if (want_reset_all) {                               /* the second SET on "Domyślne wszystkich filmów" */
        int k;
        want_reset_all = 0;
        reset_armed = 0;
        k = reset_all();
        dirty |= D_LISTALL;
        d |= D_LISTALL;
        want_save = 1;
        flog("defaults of all films restored, films changed", k);
        msg_id = k == 0 ? S_MSG_ALRDY : S_MSG_RSTA;
    }
    if (arm_new) {                                      /* the first SET: ask for the second one (the row itself changed its text, see draw_reset_all) */
        arm_new = 0;
        msg_until = get_ms_clock() + 5000;
        BMP_LOCK ( ui_attach(); draw_hint(TR(S_MSG_ARM)); )
    }
    if (reset_armed && get_ms_clock() - arm_t > 5000) {   /* nobody pressed the second SET: the question is gone */
        reset_armed = 0;
        d |= D_ROW(AROW); dirty |= D_ROW(AROW);
    }
    if (want_save) {
        want_save = 0;
        save_state();
        msg_until = get_ms_clock() + 1500;
        BMP_LOCK ( ui_attach(); draw_hint(TR(msg_id)); )
        msg_id = S_MSG_SAVED;
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
    {                                                   /* the progress line grows by itself, too */
        int pv = dev_prog_now() / 5 * 5;                /* in steps of 0.5 %: a few redraws per second at most */
        if (pv != prog_shown) { d |= D_PROG; dirty |= D_PROG; }
    }
    if (!d) return;
    dirty &= ~d;
    if (d & D_FULL) {                                   /* the colours or the language changed: new palette entries / texts, the whole panel again */
        BMP_LOCK ( ui_attach(); pal_build(); pal_apply(); draw_static(); paint_rows(); draw_status(st_last); draw_prog(prog_shown > 0 ? prog_shown : 0); )
        want_save = 1;                                  /* the choice is kept at once */
        return;
    }
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
        if (d & D_PROG)    { prog_shown = dev_prog_now() / 5 * 5; draw_prog(prog_shown); }
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
    st_set = 0;                                         /* the panel always opens on the film, never on the settings */
    row = (st_film > 0) ? 2 : 0;                        /* start on the film row when a film is chosen */
    top = 0;
    list_build();
    klog_n = 0;
    msg_until = 0;
    want_save = 0;
    want_reset = 0;
    msg_id = S_MSG_SAVED;
    reset_armed = 0; arm_new = 0; want_reset_all = 0;
    strcpy(st_last, "?");
    prog_shown = -1;
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
    if (row == 0) {                                      /* tabs: 0 = no effect, 1.. = colour / b&w / slide / own, the last one = Ustawienia */
        int ct = cur_tab();
        int nt = clampi(ct + dir, 0, TAB_SET);
        if (nt != ct && (nt == TAB_SET || ct == TAB_SET)) {   /* to / from the settings: the chosen film (an own preset) stays chosen */
            st_set = (nt == TAB_SET);
            list_build();
            top = 0;
            dirty |= D_ROW(0) | D_ROW(1) | D_ROW(2) | D_INFO | D_LISTALL;
        } else if (nt != ct) {
            remember();
            if (nt == 0) st_film = 0;
            else {
                int g = types[nt - 1].first_group + clampi(memo_group[nt - 1], 0, types[nt - 1].ngroups - 1);
                st_film = 1 + groups[g].first + clampi(memo_film[g], 0, groups[g].n - 1);
            }
            dirty |= D_ROW(0) | D_ROW(1) | D_ROW(2) | D_INFO | film_changed() | D_LISTALL;
        }
    } else if (row == 2) {                               /* film inside the type (the makers follow each other, row 1 only shows the maker) */
        if (st_film > 0) {
            int t = groups[films[st_film - 1].group].type;
            int nf = 1 + clampi(st_film - 1 + dir, type_first(t), type_first(t) + type_films(t) - 1);
            if (nf != st_film) {
                int g0 = films[st_film - 1].group;
                st_film = nf;
                remember();
                dirty |= D_ROW(2) | D_INFO | film_changed();
                if (films[nf - 1].group != g0) dirty |= D_ROW(1);
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
        if (nv != st_jpg) { st_jpg = nv; dirty |= D_ROW(JROW) | D_ROW(PROW); }
    }
    else if (row == PROW) {                              /* where the JPG goes */
        int nv = clampi(st_place + dir, 0, 2);
        if (nv != st_place) { st_place = nv; dirty |= D_ROW(PROW); }
    }
    else if (row == TROW) {                              /* the look of the panel */
        int nv = clampi(st_theme + dir, 0, 1);
        if (nv != st_theme) { st_theme = nv; dirty |= D_FULL; }
    }
    else if (row == LROW) {                              /* the language of the panel */
        int nv = clampi(st_lang + dir, 0, NLANG - 1);
        if (nv != st_lang) { st_lang = nv; strcpy(st_last, "?"); dirty |= D_FULL; }
    }
    else if (row == AROW) {                              /* the dial does nothing here: SET (twice) restores the defaults of all films */
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
        if (reset_armed) { reset_armed = 0; msg_until = 1; }           /* left the row after the first SET: the question is gone (and its hint) */
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

/* rows that can not be changed right now are skipped: the maker row (it only shows), the film without a film, the rows of the list that the
 * chosen film does not have (nothing at all without a film), push-pull for a film without variants */
static int row_enabled(int r)
{
    if (r == 0) return 1;
    if (r == 1) return 0;
    if (r == 2) return st_film > 0 && !st_set;
    return lst_pos(r) >= 0;
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
        if (row == RROW) want_reset = 1;
        else if (row == AROW) {                                 /* the first SET asks, the second one (within 5 s) does it */
            if (reset_armed) want_reset_all = 1;
            else { reset_armed = 1; arm_t = get_ms_clock(); arm_new = 1; dirty |= D_ROW(AROW); }
        }
        else want_save = 1;
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
    int place;                   /* 0 = FILMLOOK folder, 1 = replace the camera's JPG */
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
            q->film = eff_film(); cur_look(&q->look); q->jpg = st_jpg; q->place = st_place;
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
#define REPL_NAME  "FILMLOOK/REPLACE.TMP"
#define PREV_NAME  "FILMLOOK/PREVIEW.TMP"
#define READ_CHUNK (1024 * 1024)
typedef struct {
    int used, tries, film, mode, number, t_shot, place;
    char dir[40], prefix[8];
    look_t look;
} dev_job_t;
static dev_job_t dev_q[DEV_Q];
static int dev_task_started = 0;
static volatile int dev_busy = 0;            /* a job is running right now */
static volatile int dev_last_ms = 0;         /* how long the last finished JPG took (ms), 0 = none yet */
static volatile int dev_done_n = 0;          /* finished JPGs since the camera was switched on */
static volatile int dev_phase = 0;           /* 0 nothing, 1 waits for the camera, 2 makes the JPG, 3 writes it to the card */
static volatile int dev_prog = 0;            /* how far the JPG that is made now is, 0..1000 (the line under the title) */
static volatile int dev_end_ms = -100000;    /* when the last JPG was finished: the full line stays a moment */
static int gate_waited = 0;                  /* ms this job waited for the camera */

static volatile int cam_job = 0;             /* PROP_LAST_JOB_STATE: 0 = the camera is idle, 8 = writing to the card, B = busy */
static volatile int last_shot_ms = -100000;  /* when the last photo was taken */
static volatile int cover_base = -1;         /* PROP_CARD_COVER as it was when the camera started (cover closed) ... */
static volatile int cover_now = -1;          /* ... and now: a different value means the card cover is open */

PROP_HANDLER(PROP_LAST_JOB_STATE) { cam_job = buf[0]; }
PROP_HANDLER(PROP_CARD_COVER)
{
    int v = (int)buf[0];
    cover_now = v;
    if (cover_base < 0) cover_base = v;
    else if (v != cover_base) dlog_add("card cover property changed to", v, cover_base, 0);
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
    int t = get_ms_clock(), ph = dev_phase, waited = 0;
    while ((cam_job || get_ms_clock() - last_shot_ms < 2500) && dev_alive() && gate_left > 0) {
        if (!waited) { waited = 1; dev_phase = 1; }
        msleep(50); gate_left -= 50;
    }
    if (waited) { gate_waited += get_ms_clock() - t; dev_phase = ph; }
    return dev_alive() ? 0 : 1;
}

/* the line under the title: how far the JPG is (0 = nothing is made), and for 2 s after the end it stays full */
static int dev_prog_now(void)
{
    if (dev_busy) return dev_prog;
    if (get_ms_clock() - dev_end_ms < 2000) return 1000;
    return 0;
}

/* a replacement writes over the camera's JPG: not while the camera shows pictures (playback, quick review) */
static int dev_gate_play(void)
{
    int t = get_ms_clock();
    while ((is_play_mode() || QR_MODE) && dev_alive() && gate_left > 0) { msleep(100); gate_left -= 100; }
    gate_waited += get_ms_clock() - t;
    return dev_alive() ? 0 : 1;
}

static struct {
    FILE *in, *out;
    uint8_t *ibuf, *obuf, *big;
    uint32_t ibase, ilen, icap;              /* the window of the source file that is in RAM: where it starts, how long it is, how big it can be */
    int ms_read, ms_write, inram, place_repl; /* ... place_repl: the camera's JPG is replaced afterwards (the progress is shared by two writes) */            /* time spent on the card (reading / writing), and: the whole JPG is in RAM */
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
        if (off < dio.ibase || off >= dio.ibase + dio.ilen) {          /* the window does not cover it: refill (as much as fits in RAM) */
            uint32_t have = 0;
            int t = 0;
            if (dev_gate()) return 1;
            t = get_ms_clock();
            if (FIO_SeekSkipFile(dio.in, off, SEEK_SET) < 0) return 1;
            while (have < dio.icap) {
                uint32_t want = dio.icap - have;
                int got;
                if (want > READ_CHUNK) want = READ_CHUNK;
                got = FIO_ReadFile(dio.in, dio.ibuf + have, want);
                if (got <= 0) break;
                have += (uint32_t)got;
                if ((uint32_t)got < want) break;                        /* end of the file */
            }
            dio.ms_read += get_ms_clock() - t;
            if (have == 0) { dio.ilen = 0; return 1; }
            dio.ibase = off; dio.ilen = have;
            if (dev_prog < 25) dev_prog = 25;
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
    dev_phase = 3;
    {
        int t = get_ms_clock();
        uint32_t all = dio.len;
        while (o < all) {
            uint32_t c = all - o;
            if (c > OUT_CHUNK) c = OUT_CHUNK;
            if (!dev_alive()) return 1;
            if (FIO_WriteFile(dio.out, dio.big + o, c) != (int)c) { flog_s("JPG: card write failed", dio.dst); return 1; }
            o += c;
            if (dio.inram) dev_prog = 930 + (int)((o * (dio.place_repl ? 35u : 70u)) / all);
        }
        dio.ms_write += get_ms_clock() - t;
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
    uint8_t *t;
    FILE *f;
    int ok = 0;
    if (FIO_GetFileSize(path, &size) != 0 || size < 2000) return 0;
    t = (uint8_t *)fio_malloc(16);                       /* not a stack variable: see read_text */
    if (!t) return 0;
    f = FIO_OpenFile(path, O_RDONLY | O_SYNC);
    if (f) {
        t[0] = t[1] = 0;
        if (FIO_SeekSkipFile(f, size - 2, SEEK_SET) >= 0 && FIO_ReadFile(f, t, 2) == 2 && t[0] == 0xFF && t[1] == 0xD9) ok = 1;
        FIO_CloseFile(f);
    }
    fio_free(t);
    return ok;
}

/* the camera's own JPG is replaced by the finished one. The finished JPG is already complete in FILMLOOK/ (dio.dst) and stays
 * there; while the camera's file is being rewritten FILMLOOK/REPLACE.TMP names both files, so after an interruption the
 * next start can put the complete copy back (dev_recover). 0 = replaced and checked, 1 = not done (the camera's JPG is
 * untouched, or has been restored from the copy) */
static int dev_replace(const char *target, const char *root)
{
    char note[48], txt[200];
    FILE *m, *o;
    uint32_t pos = 0, got = 0;
    int t = get_ms_clock(), ok = 0, werr = 0;

    if (!dio.big || !dio.inram || dio.cap < dio.total || dio.total < 2000) return 1;
    if (dev_gate_play()) return 1;
    if (is_play_mode() || QR_MODE) { flog("JPG: the camera is showing pictures, the JPG stays in FILMLOOK", 0); return 1; }
    if (dev_gate()) return 1;
    dev_phase = 3;
    snprintf(note, sizeof(note), "%s%s", root, REPL_NAME);
    snprintf(txt, sizeof(txt), "%s\n%s\nJ\n", dio.dst, target);
    m = FIO_CreateFile(note);
    if (!m) { flog_s("JPG: cannot make the replace note", note); return 1; }
    FIO_WriteFile(m, txt, strlen(txt));
    FIO_CloseFile(m);
    o = FIO_CreateFile(target);                          /* the camera's JPG is gone from here until it is written again */
    if (o) {
        while (pos < dio.total) {
            uint32_t c = dio.total - pos;
            if (c > OUT_CHUNK) c = OUT_CHUNK;
            if (!dev_alive()) { FIO_CloseFile(o); flog_s("JPG: stopped while replacing, the copy in FILMLOOK is complete", target); return 1; }
            if (FIO_WriteFile(o, dio.big + pos, c) != (int)c) { werr = 1; break; }
            pos += c;
            dev_prog = 965 + (int)((pos * 35u) / dio.total);
        }
        FIO_CloseFile(o);
    } else werr = 1;
    dio.ms_write += get_ms_clock() - t;
    if (!werr && FIO_GetFileSize(target, &got) == 0 && got == dio.total && jpg_complete(target)) ok = 1;
    if (!ok) {                                           /* put the complete copy back, then the note can go */
        flog_s("JPG: replacing failed, restoring from the copy", target);
        FIO_RemoveFile(target);
        if (FIO_CopyFile(dio.dst, target) != 0) { flog_s("JPG: RESTORE FAILED, the complete copy is", dio.dst); return 1; }
    }
    FIO_RemoveFile(note);
    return ok ? 0 : 1;
}

/* ---- the preview inside the RAW file ------------------------------------------------------------------------------
 * Canon shows a RAW picture on the camera's screen from the full size JPEG that every CR2 carries (the file's first IFD:
 * its offset, its length and its size are three numbers in the first 300 bytes). When "podgląd RAW" is chosen, the
 * finished JPG is written over that JPEG IN PLACE. The file does not change its length, no sector of the card's file
 * table moves, the RAW data (the sensor values) are not touched at all. Only the numbers for the preview's own length and
 * size are updated. A copy of the finished JPG stays in FILMLOOK/ and REPLACE.TMP names it while the patch is made, so a
 * start after an interruption can do the patch again (it can be repeated any number of times). */
typedef struct { uint32_t so, sb, off_w, off_h, off_sb, ty_w, ty_h; } cr2_info;      /* ty_*: 3 = the number is 16 bit, 4 = 32 bit */

/* width and height of a JPG in RAM, from its SOF marker. 0 = not found */
static int jpg_dims(const uint8_t *d, uint32_t len, int *w, int *h)
{
    uint32_t i = 2;
    while (i + 9 < len) {
        uint32_t m;
        if (d[i] != 0xFF) return 0;
        m = d[i + 1];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            return *w > 0 && *h > 0;
        }
        i += 2 + ((d[i + 2] << 8) | d[i + 3]);
    }
    return 0;
}

static void put_le(uint8_t *b, uint32_t v, int bytes) { int i; for (i = 0; i < bytes; i++) b[i] = (uint8_t)(v >> (8 * i)); }

/* looks at the CR2: 0 = the finished JPG of `len` bytes fits over its preview, 1 = it does not (nothing is changed) */
static int cr2_check(const char *cr2, uint32_t len, cr2_info *ci)
{
    uint32_t fsize = 0, ifd, n, i, comp = 0;
    uint8_t *hd;
    FILE *f;
    int got = 0;
    memset(ci, 0, sizeof *ci);
    if (FIO_GetFileSize(cr2, &fsize) != 0 || fsize < 100000) return 1;
    hd = (uint8_t *)fio_malloc(1024);
    if (!hd) return 1;
    f = FIO_OpenFile(cr2, O_RDONLY | O_SYNC);
    if (f) { got = FIO_ReadFile(f, hd, 1024); FIO_CloseFile(f); }
    if (got < 64 || hd[0] != 'I' || hd[1] != 'I' || hd[2] != 42 || hd[8] != 'C' || hd[9] != 'R') { fio_free(hd); flog_s("RAW preview: not a CR2", cr2); return 1; }
    ifd = fl_r32(hd + 4, 0);
    n = (ifd + 2 <= (uint32_t)got) ? fl_r16(hd + ifd, 0) : 0;
    if (n > 60 || ifd + 2 + 12 * n > (uint32_t)got) { fio_free(hd); flog_s("RAW preview: unknown file layout", cr2); return 1; }
    for (i = 0; i < n; i++) {
        const uint8_t *e = hd + ifd + 2 + 12 * i;
        uint32_t tag = fl_r16(e, 0), type = fl_r16(e + 2, 0), vo = ifd + 2 + 12 * i + 8;
        if (tag == 0x0100) { ci->off_w = vo; ci->ty_w = type; }
        else if (tag == 0x0101) { ci->off_h = vo; ci->ty_h = type; }
        else if (tag == 0x0103) comp = fl_r32(e + 8, 0) & 0xFFFF;
        else if (tag == 0x0111) ci->so = fl_r32(e + 8, 0);
        else if (tag == 0x0117) { ci->sb = fl_r32(e + 8, 0); ci->off_sb = vo; if (type != 4) ci->off_sb = 0; }
    }
    fio_free(hd);
    if (comp != 6 || !ci->so || !ci->sb || !ci->off_w || !ci->off_h || !ci->off_sb || (ci->ty_w != 3 && ci->ty_w != 4) || (ci->ty_h != 3 && ci->ty_h != 4)) {
        flog_s("RAW preview: no JPEG preview found", cr2); return 1;
    }
    if (ci->so + ci->sb > fsize) { flog_s("RAW preview: file is shorter than its preview", cr2); return 1; }
    if (len > ci->sb) { flog("RAW preview: the new JPG is longer than the old preview, bytes more", (int)(len - ci->sb)); return 1; }
    return 0;
}

/* writes the JPG over the preview, then the new size numbers, then checks it. 0 = done, 2 = it did not work out (the note stays) */
static int cr2_write(const char *cr2, const cr2_info *ci, const uint8_t *data, uint32_t len)
{
    uint8_t *hd = (uint8_t *)fio_malloc(16);
    FILE *f;
    uint32_t pos = 0;
    int ok = 1, w = 0, h = 0;
    if (!hd) return 2;
    if (!jpg_dims(data, len, &w, &h) || (ci->ty_w == 3 && w > 65535) || (ci->ty_h == 3 && h > 65535)) { fio_free(hd); return 2; }
    f = FIO_OpenFile(cr2, O_RDWR | O_SYNC);
    if (!f) { fio_free(hd); return 2; }
    if (FIO_SeekSkipFile(f, ci->so, SEEK_SET) < 0) ok = 0;
    while (ok && pos < len) {
        uint32_t c = len - pos;
        if (c > OUT_CHUNK) c = OUT_CHUNK;
        if (!dev_alive()) { ok = 0; break; }
        if (FIO_WriteFile(f, data + pos, c) != (int)c) ok = 0;
        pos += c;
        { int v = 965 + (int)((pos * 35u) / len); if (v > dev_prog) dev_prog = v; }
    }
    if (ok) {                                            /* the numbers go last: until then the old ones still describe the file */
        int bw = ci->ty_w == 3 ? 2 : 4, bh = ci->ty_h == 3 ? 2 : 4;
        put_le(hd, (uint32_t)w, bw);
        if (FIO_SeekSkipFile(f, ci->off_w, SEEK_SET) < 0 || FIO_WriteFile(f, hd, bw) != bw) ok = 0;
        put_le(hd, (uint32_t)h, bh);
        if (FIO_SeekSkipFile(f, ci->off_h, SEEK_SET) < 0 || FIO_WriteFile(f, hd, bh) != bh) ok = 0;
        put_le(hd, len, 4);
        if (FIO_SeekSkipFile(f, ci->off_sb, SEEK_SET) < 0 || FIO_WriteFile(f, hd, 4) != 4) ok = 0;
    }
    FIO_CloseFile(f);
    if (ok) {                                            /* read back: the first and last bytes of the JPEG and its length */
        f = FIO_OpenFile(cr2, O_RDONLY | O_SYNC);
        ok = 0;
        if (f) {
            uint32_t n = 0;
            if (FIO_SeekSkipFile(f, ci->so, SEEK_SET) >= 0 && FIO_ReadFile(f, hd, 2) == 2 && hd[0] == 0xFF && hd[1] == 0xD8 &&
                FIO_SeekSkipFile(f, ci->so + len - 2, SEEK_SET) >= 0 && FIO_ReadFile(f, hd, 2) == 2 && hd[0] == 0xFF && hd[1] == 0xD9 &&
                FIO_SeekSkipFile(f, ci->off_sb, SEEK_SET) >= 0 && FIO_ReadFile(f, hd, 4) == 4) {
                n = fl_r32(hd, 0);
                ok = (n == len);
            }
            FIO_CloseFile(f);
        }
    }
    fio_free(hd);
    return ok ? 0 : 2;
}

/* the JPG `data` (in RAM) becomes the preview of the CR2. It is first written to FILMLOOK/PREVIEW.TMP, a note names that file and
 * the CR2, and only then the CR2 is touched; when all is checked, the note and the file go. 0 = done, 1 = not done (nothing
 * changed), 2 = interrupted or failed (the note and the file stay: the next start repairs it) */
static int dev_patch_preview(const char *cr2, const char *root, const uint8_t *data, uint32_t len)
{
    char note[48], tmp[48], txt[200];
    cr2_info ci;
    FILE *m;
    uint32_t pos = 0, got = 0;
    int t = get_ms_clock(), rc, werr = 0;
    if (len < 2000 || cr2_check(cr2, len, &ci) != 0) return 1;
    if (dev_gate_play()) return 1;
    if (is_play_mode() || QR_MODE) { flog("RAW preview: the camera is showing pictures, not now", 0); return 1; }
    if (dev_gate()) return 1;
    dev_phase = 3;
    snprintf(note, sizeof(note), "%s%s", root, REPL_NAME);
    snprintf(tmp, sizeof(tmp), "%s%s", root, PREV_NAME);
    m = FIO_CreateFile(tmp);                             /* the copy first: it is what a repair is made from */
    if (!m) { flog_s("RAW preview: cannot make", tmp); return 1; }
    while (pos < len) {
        uint32_t c = len - pos;
        if (c > OUT_CHUNK) c = OUT_CHUNK;
        if (!dev_alive() || FIO_WriteFile(m, data + pos, c) != (int)c) { werr = 1; break; }
        pos += c;
    }
    FIO_CloseFile(m);
    if (werr || FIO_GetFileSize(tmp, &got) != 0 || got != len || !jpg_complete(tmp)) {
        flog_s("RAW preview: the copy is not complete, the RAW is untouched", tmp);
        FIO_RemoveFile(tmp);
        return 1;
    }
    snprintf(txt, sizeof(txt), "%s\n%s\nP\n", tmp, cr2);
    m = FIO_CreateFile(note);
    if (!m) { flog_s("RAW preview: cannot make the note", note); FIO_RemoveFile(tmp); return 1; }
    FIO_WriteFile(m, txt, strlen(txt));
    FIO_CloseFile(m);
    rc = cr2_write(cr2, &ci, data, len);
    if (rc != 0 && dev_alive()) rc = cr2_write(cr2, &ci, data, len);        /* once more: the data is still in RAM */
    dio.ms_write += get_ms_clock() - t;
    if (rc == 0) { FIO_RemoveFile(note); FIO_RemoveFile(tmp); }
    else flog_s("RAW preview: NOT finished (it is repaired at the next start from the copy)", tmp);
    return rc;
}

/* ---- a smaller picture for the RAW ----
 * The preview inside the RAW is often smaller than the finished JPG (this one is 0.9 MB, another 3 MB). The finished JPG is
 * then decoded at half the size (the decoder does that while it decodes: it is quick) and written again with a lower
 * quality until it fits. No film look is applied again: it is the finished picture, only smaller. */
typedef struct { uint8_t *buf; uint32_t cap, len; se_jpeg *enc; } shrink_t;

static int shrink_sink(void *ctx, const uint8_t *data, size_t n)
{
    shrink_t *s = (shrink_t *)ctx;
    if (s->len + n > s->cap) return 1;                   /* does not fit */
    memcpy(s->buf + s->len, data, n);
    s->len += (uint32_t)n;
    return 0;
}

static int shrink_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    (void)width;
    return se_jpeg_write_rows(((shrink_t *)ctx)->enc, rgb, nrows) == SE_OK ? 0 : -1;
}

/* 0 = made (len bytes in dst), else it does not fit / failed */
static int shrink_jpg(const uint8_t *src, uint32_t slen, uint8_t *dst, uint32_t dcap, int quality, uint8_t *mem, size_t msz, uint32_t *len)
{
    se_arena ar;
    se_jdec_info ji;
    shrink_t s;
    uint8_t *strip;
    int ow, oh;
    se_err rc;
    se_arena_init(&ar, mem, msz);
    rc = se_jpeg_dec_info(src, slen < FL_HDR_BYTES ? slen : FL_HDR_BYTES, &ji);
    if (rc != SE_OK || ji.width < 800) return 1;
    se_jpeg_scaled_size(ji.width, ji.height, 2, &ow, &oh);
    s.buf = dst; s.cap = dcap; s.len = 0;
    s.enc = SE_NEW(&ar, se_jpeg, 1);
    strip = SE_NEW(&ar, uint8_t, SE_JPEG_STRIP_BYTES(ow));
    if (!s.enc || !strip) return 1;
    rc = se_jpeg_begin(s.enc, ow, oh, quality, strip, 0, shrink_sink, &s);
    if (rc == SE_OK) rc = se_jpeg_decode_scaled(src, slen, 2, &ar, shrink_rows, &s, &ji);
    if (rc == SE_OK) rc = se_jpeg_end(s.enc);
    if (rc != SE_OK) return 1;
    *len = s.len;
    return 0;
}

/* the finished picture (dio.big) goes into the RAW as its preview. 0 = done, 1 = not done, 2 = interrupted (repair at start) */
static int dev_preview_for_raw(const char *cr2, const char *root, uint8_t *mem, size_t msz)
{
    cr2_info ci;
    uint8_t *pv;
    uint32_t n = 0;
    static const int qual[3] = { 86, 76, 66 };
    int i, rc = 1;
    if (!dio.big || !dio.inram || dio.cap < dio.total || dio.total < 2000) return 1;
    if (cr2_check(cr2, 0, &ci) != 0) return 1;           /* the layout of the RAW and the room for the preview (ci.sb) */
    if (dio.total <= ci.sb) return dev_patch_preview(cr2, root, dio.big, dio.total);
    pv = (uint8_t *)fio_malloc(ci.sb);
    if (!pv) { flog("RAW preview: no memory for a smaller picture, KB", (int)(ci.sb >> 10)); return 1; }
    for (i = 0; i < 3; i++) {
        if (!dev_alive()) break;
        if (shrink_jpg(dio.big, dio.total, pv, ci.sb, qual[i], mem, msz, &n) == 0) {
            flog("RAW preview: smaller picture made, bytes", (int)n);
            flog("RAW preview: its quality", qual[i]);
            rc = dev_patch_preview(cr2, root, pv, n);
            break;
        }
    }
    if (rc == 1 && i == 3) flog("RAW preview: even the smallest picture does not fit, bytes of room", (int)ci.sb);
    fio_free(pv);
    return rc;
}

/* at start: a note left behind means a write was interrupted (power lost, battery out): look at the file it names */
static void dev_recover(void)
{
    static char buf[200];
    int n = read_text(MARK_NAME, buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n > 0 ? n : 0] = 0;
        if (n > 0 && !jpg_complete(buf)) { FIO_RemoveFile(buf); flog_s("JPG: removed an unfinished file", buf); }
        else if (n > 0) flog_s("JPG: the file named in the note is complete, kept", buf);
        FIO_RemoveFile(MARK_NAME);
    }
    n = read_text(REPL_NAME, buf, sizeof(buf) - 1);      /* a replacement of the camera's JPG / of a RAW preview was interrupted */
    if (n >= 0) {
        char *copy = buf, *target = 0, *kind = 0, *e;
        buf[n > 0 ? n : 0] = 0;
        for (e = buf; *e && *e != '\n'; e++) {}
        if (*e) { *e = 0; target = e + 1; for (e = target; *e && *e != '\n'; e++) {} if (*e) { *e = 0; kind = e + 1; } }
        if (target && target[0] && kind && kind[0] == 'P') {              /* RAW preview: do the patch again from the complete copy */
            uint32_t size = 0;
            cr2_info ci;
            if (FIO_GetFileSize(copy, &size) == 0 && size >= 2000 && size < (16u << 20) && jpg_complete(copy) && cr2_check(target, size, &ci) == 0) {
                uint8_t *d = (uint8_t *)fio_malloc(size);
                FILE *f = d ? FIO_OpenFile(copy, O_RDONLY | O_SYNC) : 0;
                uint32_t have = 0;
                int ok = 0;
                if (f) {
                    while (have < size) {
                        int got = FIO_ReadFile(f, d + have, size - have > READ_CHUNK ? READ_CHUNK : size - have);
                        if (got <= 0) break;
                        have += (uint32_t)got;
                    }
                    FIO_CloseFile(f);
                    ok = (have == size);
                }
                if (ok && cr2_write(target, &ci, d, size) == 0) { flog_s("RAW preview: the interrupted patch was finished from the copy", target); FIO_RemoveFile(copy); }
                else flog_s("RAW preview: could not finish the patch, the complete copy is", copy);
                if (d) fio_free(d);
            } else flog_s("RAW preview: note found, nothing to repair", target);
        } else if (target && target[0] && jpg_complete(copy) && !jpg_complete(target)) {      /* the camera's JPG */
            FIO_RemoveFile(target);
            if (FIO_CopyFile(copy, target) == 0) flog_s("JPG: the interrupted replacement was finished from the copy", target);
            else flog_s("JPG: could not finish the replacement, the complete copy is", copy);
        } else flog("JPG: replace note found, nothing to repair", 0);
        FIO_RemoveFile(REPL_NAME);
    }
}

/* the rows of the picture that the film look has finished: the progress of the line under the title (30 .. 930 of 1000;
 * before it the source is read, after it the JPG is written) */
static void dev_progress_cb(void *ctx, int permille)
{
    int v = 30 + (permille * 9) / 10;
    (void)ctx;
    if (v > dev_prog) dev_prog = v;
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
    int t0 = 0, t_proc = 0, ok = 0, retry = 0, i, root, src_jpg = 0, replaced = -1, patched = -1, want_prev = 0;
    char cr2[96];

    dev_phase = 2; dev_prog = 3; gate_waited = 0; dev_end_ms = -100000;
    snprintf(src, sizeof(src), "%s%s%04d.JPG", j->dir, j->prefix, j->number);        /* the camera's JPG, or ... */
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000)
        snprintf(src, sizeof(src), "%s%s%04d.CR2", j->dir, j->prefix, j->number);   /* ... the preview inside the RAW */
    else if (!jpg_complete(src)) { flog_s("JPG: the camera has not finished writing", src); return 1; }
    else src_jpg = 1;
    if (FIO_GetFileSize(src, &size) != 0 || size < 2000) { flog_s("JPG: no source file", src); return 0; }
    for (i = 0; j->dir[i] && j->dir[i] != '/'; i++) {}
    root = j->dir[i] == '/' ? i + 1 : 0;                                              /* "B:/" */
    for (i = 0; i < root && i < 7; i++) r[i] = j->dir[i];
    r[i] = 0;
    snprintf(dio.dst, sizeof(dio.dst), "%sFILMLOOK/%s%04d.JPG", r, j->prefix, j->number);
    snprintf(dio.mark, sizeof(dio.mark), "%s%s", r, MARK_NAME);
    snprintf(cr2, sizeof(cr2), "%s%s%04d.CR2", j->dir, j->prefix, j->number);
    if (j->place >= 1 && !src_jpg) flog("JPG: nothing to replace (RAW only), the file goes to FILMLOOK", j->number);
    if (j->place == 2) {
        uint32_t cs = 0;
        want_prev = (FIO_GetFileSize(cr2, &cs) == 0 && cs >= 100000);
        if (!want_prev) flog("RAW preview: there is no RAW file of this picture", j->number);
    }

    mem = (uint8_t *)malloc(need);
    dio.ibuf = 0; dio.icap = 0;
    dio.obuf = (uint8_t *)fio_malloc(IOBUF);
    dio.big = 0; dio.cap = 0;
    if (mem && dio.obuf) {
        uint32_t c;                                      /* room for the whole finished JPG, as much as there is */
        for (c = (j->mode == 2) ? 12u << 20 : 4u << 20; c >= (1u << 20) && !dio.big; c >>= 1)
            if ((dio.big = (uint8_t *)fio_malloc(c)) != 0) dio.cap = c;
        for (c = 8u << 20; c >= IOBUF && !dio.ibuf; c >>= 1)      /* and room for the source: it is read in one go, so */
            if ((dio.ibuf = (uint8_t *)fio_malloc(c)) != 0) dio.icap = c;      /* the card is not touched while the picture is made */
    }
    if (!mem || !dio.ibuf || !dio.obuf) {
        if (mem) free(mem);
        if (dio.ibuf) fio_free(dio.ibuf);
        if (dio.obuf) fio_free(dio.obuf);
        if (dio.big) fio_free(dio.big);
        flog("JPG: no memory now, KB needed", (int)(need >> 10));
        return 1;
    }
    dio.in = FIO_OpenFile(src, O_RDONLY | O_SYNC);
    dio.out = 0; dio.stream = 0;
    dio.ibase = 0; dio.ilen = 0; dio.on = 0; dio.len = 0; dio.total = 0;
    dio.ms_read = 0; dio.ms_write = 0; dio.inram = 0; dio.place_repl = ((j->place >= 1 && src_jpg) || want_prev);
    if (!dio.in) {
        flog_s("JPG: cannot open", src);
    } else {
        t0 = get_ms_clock();
        memset(&io, 0, sizeof io);
        io.read = dev_read; io.rctx = 0; io.src_size = size;
        io.write = dev_write; io.wctx = 0;
        io.mem = mem; io.mem_size = need;
        io.progress = dev_progress_cb; io.pctx = 0;
        fl_develop(&io, f, &j->look, scale, 90, &res);
        t_proc = get_ms_clock() - t0;
        FIO_CloseFile(dio.in);
        if (want_prev && dio.ibuf) { fio_free(dio.ibuf); dio.ibuf = 0; }     /* room for the smaller picture of the RAW */
        if (res.rc == 0 && !dev_alive()) res.rc = SE_E_IO;
        if (res.rc == 0) {                               /* the short part that writes: the rest of the picture, then close */
            if (!dio.stream) { dio.inram = 1; if (dev_open_out()) res.rc = SE_E_IO; }
            else if (dev_flush()) res.rc = SE_E_IO;
        }
        if (dio.out) FIO_CloseFile(dio.out);
        if (res.rc == 0) {                               /* check what is on the card, then take the note away */
            uint32_t got = 0;
            if (FIO_GetFileSize(dio.dst, &got) == 0 && got == dio.total && jpg_complete(dio.dst)) ok = 1;
            else { flog_s("JPG: the file on the card does not match", dio.dst); res.rc = SE_E_IO; }
        }
        if (ok) {
            FIO_RemoveFile(dio.mark);
            if (j->place >= 1 && src_jpg) replaced = dev_replace(src, r);       /* the camera's own JPG becomes the finished one */
            if (want_prev) patched = dev_preview_for_raw(cr2, r, mem, need);        /* and the preview inside the RAW */
            {
                int all = get_ms_clock() - t0;
                dev_last_ms = all - gate_waited; if (dev_last_ms < 1) dev_last_ms = 1;
                dev_done_n++;
                dev_prog = 1000; dev_end_ms = get_ms_clock();
                flog_s("JPG written", dio.dst);
                if (j->place >= 1 && src_jpg) flog(replaced == 0 ? "JPG: the camera's JPG was REPLACED" : "JPG: the camera's JPG was NOT replaced (copy kept in FILMLOOK)", j->number);
                if (want_prev) flog(patched == 0 ? "RAW preview: PATCHED (the camera shows the finished picture)" : "RAW preview: NOT patched (copy kept in FILMLOOK)", j->number);
                flog("JPG width", res.out_w);
                flog("JPG ms the picture itself (decode, film, encode)", t_proc - gate_waited > 0 ? t_proc - gate_waited : t_proc);
                flog("JPG ms waiting for the camera", gate_waited);
                flog("JPG ms reading the source from the card", dio.ms_read);
                flog("JPG ms writing to the card", dio.ms_write);
                flog("JPG ms all, without the waiting", dev_last_ms);
                flog("JPG ms since the shot", get_ms_clock() - j->t_shot);
                if (ui_state == UI_CLOSED && !is_play_mode() && !QR_MODE) {          /* a short note on the screen: the JPG is ready (nothing is drawn over the panel or the pictures) */
                    NotifyBox(3500, "Film Look: %04d ready, %d.%d s%s%s", j->number % 10000, dev_last_ms / 1000, (dev_last_ms % 1000) / 100,
                              (j->place >= 1 && src_jpg && replaced == 0) ? ", JPG replaced" : "", (want_prev && patched == 0) ? ", RAW preview" : "");
                }
                flog("JPG source window KB", (int)(dio.icap >> 10));
                flog("JPG bytes", (int)dio.total);
                flog("JPG arena peak KB", (int)(res.mem_peak >> 10));
                flog("JPG made in RAM (1) or on the card (0)", dio.big ? (dio.cap >= dio.total) : 0);
            }
        } else {
            if (dio.stream) { FIO_RemoveFile(dio.dst); FIO_RemoveFile(dio.mark); }
            if (res.rc == SE_E_TRUNCATED) { flog_s("JPG: the source is cut (still being written?), later", src); retry = 1; }
            else { flog_s("JPG failed, source", src); flog("JPG error code", res.rc); retry = 0; }
        }
    }
    if (dio.big) fio_free(dio.big);
    free(mem);
    if (dio.ibuf) fio_free(dio.ibuf);
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
            dev_phase = 0;
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
            j->film = q->film; j->mode = q->jpg; j->number = q->number; j->look = q->look; j->t_shot = q->t; j->place = q->place;
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
