/*
 * filmlook.c - film look simulation for Canon EOS 5D Mark II (Magic Lantern module)
 *
 *  INFO opens ONE full-screen panel: brand -> film (of that brand) + settings.
 *    both dials and joystick left/right = change value of the chosen row, joystick up/down = next row,
 *    SET = save, INFO / MENU = save & close, half-shutter = close and shoot.
 *  After every photo an IMG_xxxx.xmp sidecar is written (Lightroom / Camera Raw).
 *  RAW / JPEG files are never touched. No Canon property is written.
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

#include "films.h"
#include "xmp.h"

#define VERSION "v1.5"
#define LOG_PATH      "ML/LOGS/FILMLOOK.LOG"
#define SETTINGS_PATH "ML/SETTINGS/FILMLOOK.TXT"
#define NROWS 5                              /* 0 brand, 1 film, 2 effect, 3 grain, 4 contrast */

extern int gui_state;
extern int lv;
extern int set_config_var(const char *name, int new_value);
extern int get_config_var(const char *name);

/* ------------------------------------------------------------------ state */
static int st_film = 0;                   /* 0 = Off, 1.. = index into films[] + 1 */
static int st_strength = 100;             /* 0..100 */
static int st_grain = 100;                /* 0..200 */
static int st_contrast = 0;               /* -50..50 */

/* panel life cycle: opening and closing need the Canon GUI task to run, so they are done in the
 * shoot task (the key handler only requests them) */
#define UI_CLOSED   0
#define UI_OPENING  1
#define UI_OPEN     2
#define UI_CLOSING  3
static volatile int ui_state = UI_CLOSED;
static int row = 0;                       /* 0 brand, 1 film, 2 effect, 3 grain, 4 contrast */
static int last_input_ms = 0;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* films[] is grouped by brand (tools/preset2films.py sorts it). brand_first/brand_n describe each group,
 * memo remembers the last film of every brand so that switching brands back and forth returns to it */
static int brand_first[BRAND_COUNT];
static int brand_n[BRAND_COUNT];
static int memo[BRAND_COUNT];

static void brands_init(void)
{
    int i, b;
    for (b = 0; b < BRAND_COUNT; b++) { brand_first[b] = 0; brand_n[b] = 0; memo[b] = 0; }
    for (i = 0; i < FILM_COUNT; i++) {
        b = films[i].brand;
        if (b < 0 || b >= BRAND_COUNT) continue;
        if (!brand_n[b]) brand_first[b] = i;
        brand_n[b]++;
    }
}

/* 0 = no effect, 1.. = brand + 1 */
static int cur_brand(void) { return st_film > 0 ? films[st_film - 1].brand + 1 : 0; }

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
    char t[160];
    snprintf(t, sizeof(t), "film=%s\nstrength=%d\ngrain=%d\ncontrast=%d\n",
             st_film > 0 ? films[st_film - 1].full : "Off", st_strength, st_grain, st_contrast);
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
            for (i = 0; i < FILM_COUNT; i++) {
                int l = strlen(films[i].full);
                if (strncmp(p, films[i].full, l) == 0 && (p[l] == '\n' || p[l] == '\r' || p[l] == 0)) { st_film = i + 1; return; }
            }
            return;
        }
    }
}

static void load_state(void)
{
    static char buf[256];
    int n = read_file(SETTINGS_PATH, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        parse_film(buf);
        parse_after(buf, "strength", &st_strength);
        parse_after(buf, "grain", &st_grain);
        parse_after(buf, "contrast", &st_contrast);
    }
    st_film = clampi(st_film, 0, FILM_COUNT);
    if (st_film > 0) memo[films[st_film - 1].brand] = st_film - 1 - brand_first[films[st_film - 1].brand];
    st_strength = clampi(st_strength, 0, 100);
    st_grain = clampi(st_grain, 0, 200);
    st_contrast = clampi(st_contrast, -50, 50);
}

/* ------------------------------------------------------------------ drawing */
#define C_BG     COLOR_BLACK
#define C_TEXT   COLOR_WHITE
#define C_ACCENT COLOR_ORANGE
#define C_DIM    COLOR_GRAY(22)
#define C_SOFT   COLOR_GRAY(60)

/* what has to be redrawn (set from the key handler, handled in the shoot task) */
#define D_ROW(r) (1 << (r))          /* rows 0..4 */
#define D_INFO   32                  /* description block under the film row */
#define D_ALL    (D_ROW(0) | D_ROW(1) | D_ROW(2) | D_ROW(3) | D_ROW(4) | D_INFO)
static volatile int dirty = 0;

/* ---- Polish letters. The stock ML fonts have no ą ę ł ś ź ż ć ń, so ML/FONTS/FILM23.RBF (made by
 * tools/make_polish_font.py) carries them in slots 0x80..0x91. Texts in this file are plain UTF-8 and are converted
 * when drawn. Without the font file the letters are replaced by their plain ASCII look-alikes. */
static unsigned int pl_spec = 0;        /* upper 16 bits of the font spec of FILM23, 0 = not available */

static const struct { unsigned char a, b, code, ascii; } pl_map[] = {
    {0xC4,0x85,0x80,'a'}, {0xC4,0x87,0x81,'c'}, {0xC4,0x99,0x82,'e'}, {0xC5,0x82,0x83,'l'}, {0xC5,0x84,0x84,'n'},
    {0xC3,0xB3,0x85,'o'}, {0xC5,0x9B,0x86,'s'}, {0xC5,0xBA,0x87,'z'}, {0xC5,0xBC,0x88,'z'},
    {0xC4,0x84,0x89,'A'}, {0xC4,0x86,0x8A,'C'}, {0xC4,0x98,0x8B,'E'}, {0xC5,0x81,0x8C,'L'}, {0xC5,0x83,0x8D,'N'},
    {0xC3,0x93,0x8E,'O'}, {0xC5,0x9A,0x8F,'S'}, {0xC5,0xB9,0x90,'Z'}, {0xC5,0xBB,0x91,'Z'}
};

static void pl_convert(const char *in, char *out, int cap)
{
    int o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && o < cap - 1) {
        if (*p < 0x80) { out[o++] = *p++; continue; }
        {
            int k, hit = 0;
            for (k = 0; k < (int)(sizeof(pl_map) / sizeof(pl_map[0])); k++) {
                if (p[0] == pl_map[k].a && p[1] == pl_map[k].b) {
                    out[o++] = pl_spec ? (char)pl_map[k].code : (char)pl_map[k].ascii;
                    p += 2; hit = 1; break;
                }
            }
            if (!hit) { out[o++] = '?'; p++; while ((*p & 0xC0) == 0x80) p++; }
        }
    }
    out[o] = 0;
}

static void pl_load_font(void)
{
    uint32_t size = 0;
    if (FIO_GetFileSize("ML/FONTS/FILM23.RBF", &size) == 0 && size > 1000) {
        uint32_t f = font_by_name("FILM23", COLOR_WHITE, COLOR_BLACK);
        if ((f & 0xFFFF0000) != (FONT_CANON & 0xFFFF0000)) pl_spec = f & 0xFFFF0000;
    }
    flog("polish font loaded", pl_spec != 0);
}

/* prints a line of text and wipes only what is left of the previous, longer text:
 * the new text is never drawn on a cleared background, so nothing blinks */
static void text_line(int font, int fg, int x, int y, int maxx, int h, const char *s)
{
    char buf[112];
    int w;
    pl_convert(s, buf, sizeof(buf));
    if (pl_spec && (font & 0xFFFF0000) == (FONT_MED & 0xFFFF0000)) font = pl_spec;   /* Polish-capable copy of the 23 px font */
    w = bmp_printf(FONT(font, fg, C_BG), x, y, "%s", buf);
    if (w < 0) w = 0;
    if (x + w < maxx) bmp_fill(C_BG, x + w, y, maxx - (x + w), h);
}

static void draw_bar(int x, int y, int w, int v, int lo, int hi, int color)
{
    int fw = ((v - lo) * w) / (hi - lo);
    bmp_fill(C_BG, x - 4, y, w + 8, 8);             /* old knob above / below the track */
    bmp_fill(C_BG, x - 4, y + 12, w + 8, 8);
    bmp_fill(C_DIM, x, y + 8, w, 4);
    if (fw > 0) bmp_fill(color, x, y + 8, fw, 4);
    bmp_fill(color, x + fw - 4, y, 8, 20);
}

/* bottom line: key help, or a short confirmation message */
static void draw_hint(const char *msg)
{
    if (msg) text_line(FONT_MED, C_ACCENT, 40, 440, 700, 28, msg);
    else     text_line(FONT_MED, C_SOFT, 40, 440, 700, 28, "pokrętło: zmiana  joystick: wiersz  SET: zapis  INFO: zamknij");
}

static void draw_static(void)
{
    bmp_fill(C_BG, 0, 0, 720, 480);
    bmp_fill(C_ACCENT, 0, 0, 720, 6);
    bmp_printf(FONT(FONT_MED_LARGE, C_ACCENT, C_BG), 40, 20, "Film Look Simulation");
    bmp_printf(FONT(FONT_MED, C_DIM, C_BG), 600, 24, VERSION);
    bmp_fill(C_DIM, 40, 66, 640, 2);
    bmp_fill(C_DIM, 40, 244, 640, 2);
    bmp_fill(C_DIM, 40, 424, 640, 2);
    draw_hint(0);
}

/* rows 0 (brand) and 1 (film): label, < value >  */
static void draw_sel(int r)
{
    int on = (row == r);
    int y = (r == 0) ? 76 : 120;
    int off = (st_film == 0);
    int dimmed = (r == 1 && off);
    int c = dimmed ? C_DIM : (on ? C_ACCENT : C_TEXT);
    const char *val;

    if (r == 0) val = off ? "Bez efektu" : brands[films[st_film - 1].brand];
    else        val = off ? "-" : films[st_film - 1].name;

    bmp_fill(on ? C_ACCENT : C_BG, 40, y + 4, 4, 32);
    text_line(FONT_MED_LARGE, c, 64, y + 4, 224, 32, r == 0 ? "Marka" : "Film");
    bmp_printf(FONT(FONT_LARGE, dimmed ? C_DIM : (on ? C_ACCENT : C_DIM), C_BG), 230, y, "<");
    text_line(FONT_LARGE, dimmed ? C_DIM : C_TEXT, 262, y, 660, 40, val);
    bmp_printf(FONT(FONT_LARGE, dimmed ? C_DIM : (on ? C_ACCENT : C_DIM), C_BG), 668, y, ">");
}

/* kind | ISO, position in the brand, short description */
static void draw_info(void)
{
    char tmp[80];
    if (st_film > 0) {
        const film_t *f = &films[st_film - 1];
        int b = f->brand;
        if (f->iso > 0) snprintf(tmp, sizeof(tmp), "%s   |   ISO %d", f->kind, f->iso);
        else            snprintf(tmp, sizeof(tmp), "%s", f->kind);
        text_line(FONT_MED, C_ACCENT, 64, 168, 610, 28, tmp);
        snprintf(tmp, sizeof(tmp), "%d / %d", st_film - brand_first[b], brand_n[b]);
        text_line(FONT_MED, C_SOFT, 620, 168, 716, 28, tmp);
        text_line(FONT_MED, C_SOFT, 64, 198, 716, 32, f->desc);
    } else {
        text_line(FONT_MED, C_ACCENT, 64, 168, 610, 28, "Efekt filmu wyłączony");
        text_line(FONT_MED, C_SOFT, 620, 168, 716, 28, "");
        text_line(FONT_MED, C_SOFT, 64, 198, 716, 32, "Zdjęcia zostają bez zmian, plik XMP nie powstaje");
    }
}

static void draw_row(int r)          /* r = 2..4: sliders */
{
    static const char *names[3] = { "Efekt", "Ziarno", "Kontrast" };
    int i = r - 2;
    int on = (row == r);
    int c = on ? C_ACCENT : C_TEXT;
    int y = 264 + i * 50;
    char tmp[32];

    bmp_fill(on ? C_ACCENT : C_BG, 40, y + 2, 4, 28);
    text_line(FONT_MED_LARGE, c, 64, y, 240, 32, names[i]);
    if (i == 0) { draw_bar(252, y + 4, 300, st_strength, 0, 100, c);
                  snprintf(tmp, sizeof(tmp), "%d %%", st_strength); }
    else if (i == 1) { draw_bar(252, y + 4, 300, st_grain, 0, 200, c);
                  snprintf(tmp, sizeof(tmp), "%d %%", st_grain); }
    else { draw_bar(252, y + 4, 300, st_contrast, -50, 50, c);
           snprintf(tmp, sizeof(tmp), "%s%d", st_contrast >= 0 ? "+" : "", st_contrast); }
    text_line(FONT_MED_LARGE, c, 592, y, 716, 32, tmp);
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
        BMP_LOCK ( draw_hint("Zapisano"); )
        flog_s("settings saved, film", st_film > 0 ? films[st_film - 1].full : "Off");
    } else if (msg_until && get_ms_clock() > msg_until) {
        msg_until = 0;
        BMP_LOCK ( draw_hint(0); )
    }
    if (!d) return;
    dirty &= ~d;
    BMP_LOCK
    (
        if (d & D_ROW(0))  draw_sel(0);
        if (d & D_ROW(1))  draw_sel(1);
        if (d & D_INFO)    draw_info();
        if (d & D_ROW(2))  draw_row(2);
        if (d & D_ROW(3))  draw_row(3);
        if (d & D_ROW(4))  draw_row(4);
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
        draw_static();
        dirty = 0;
        draw_sel(0);
        draw_sel(1);
        draw_info();
        draw_row(2);
        draw_row(3);
        draw_row(4);
    )
}

/* Same trick as the ML menu: in photo mode the Canon INFO screen does not pass the joystick and part of the
 * dial clicks to ML, so the camera is switched to its (invisible) menu mode, where all buttons arrive.
 * The Canon menu that appears for a moment is hidden by switching the bitmap layer off meanwhile. */
static void do_open(void)
{
    int i;
    kill_ml_overlay();
    row = (st_film > 0) ? 1 : 0;                        /* start on the film row when a film is chosen */
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

static void change_value(int dir)
{
    if (row == 0) {                                      /* brand: 0 = no effect, 1.. = brands */
        int cb = cur_brand();
        int nb = clampi(cb + dir, 0, BRAND_COUNT);
        if (nb != cb) {
            if (cb > 0) memo[cb - 1] = st_film - 1 - brand_first[cb - 1];
            if (nb == 0) st_film = 0;
            else st_film = 1 + brand_first[nb - 1] + clampi(memo[nb - 1], 0, brand_n[nb - 1] - 1);
            dirty |= D_ROW(0) | D_ROW(1) | D_INFO;
        }
    } else if (row == 1) {                               /* film inside the brand */
        if (st_film > 0) {
            int b = films[st_film - 1].brand;
            int pos = clampi(st_film - 1 - brand_first[b] + dir, 0, brand_n[b] - 1);
            memo[b] = pos;
            if (1 + brand_first[b] + pos != st_film) {
                st_film = 1 + brand_first[b] + pos;
                dirty |= D_ROW(1) | D_INFO;
            }
        }
    }
    else if (row == 2) { st_strength = clampi(st_strength + dir * 5, 0, 100); dirty |= D_ROW(2); }
    else if (row == 3) { st_grain = clampi(st_grain + dir * 5, 0, 200);       dirty |= D_ROW(3); }
    else               { st_contrast = clampi(st_contrast + dir * 5, -50, 50); dirty |= D_ROW(4); }
}

static void set_row(int r)
{
    r = clampi(r, 0, NROWS - 1);
    if (r != row) { dirty |= D_ROW(row) | D_ROW(r); row = r; }      /* selection colours change */
}

/* joystick up / down; the film row is skipped while no brand is chosen */
static void move_row(int dir)
{
    int r = row + dir;
    if (r == 1 && st_film == 0) r += dir;
    set_row(r);
}

/* ------------------------------------------------------------------ keys */
static int idle_photo_mode(void)
{
    return gui_state == GUISTATE_IDLE && !lv;
}

static unsigned int filmlook_keypress(unsigned int key)
{
    if (ui_state == UI_CLOSED) {
        if (key == MODULE_KEY_INFO && idle_photo_mode()) { ui_state = UI_OPENING; return 0; }
        if (key == MODULE_KEY_TRASH && idle_photo_mode()) return 0;   /* keeps ML's big menu closed */
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
    else if (ui_state == UI_OPEN) redraw_dirty();
    return 0;
}

/* ------------------------------------------------------------------ photo -> sidecar queue */
#define QMAX 8
typedef struct {
    int used, number, t;
    char dir[40];
    char prefix[8];
    int film, strength, grain, contrast;
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
            q->film = st_film; q->strength = st_strength; q->grain = st_grain; q->contrast = st_contrast;
            q->used = 1;
            flog_s("photo queued", films[st_film - 1].full);
            return;
        }
    }
    flog("photo: queue full", number);
}

PROP_HANDLER(PROP_FILE_NUMBER_A)
{
    on_new_photo(buf[0]);
}

static void write_sidecar(qitem_t *q)
{
    static char xmp[6144];
    char path[80];
    int n;
    snprintf(path, sizeof(path), "%s%s%04d.xmp", q->dir, q->prefix, q->number);
    n = build_xmp(xmp, sizeof(xmp), &films[q->film - 1], q->strength, q->grain, q->contrast);
    if (n > 0) {
        FILE *f = FIO_CreateFile(path);
        if (f) { FIO_WriteFile(f, xmp, n); FIO_CloseFile(f); flog_s("XMP written", path); }
        else flog_s("XMP cannot create", path);
    } else flog("XMP build error", n);
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
    brands_init();
    load_state();
    kill_ml_overlay();
    flog_s("FILMLOOK module loaded", VERSION);
    pl_load_font();
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
