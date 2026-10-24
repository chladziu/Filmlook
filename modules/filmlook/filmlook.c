/*
 * filmlook.c - film look simulation for Canon EOS 5D Mark II (Magic Lantern module)
 *
 *  INFO opens ONE full-screen panel: film list + settings.
 *    rear wheel = change value of the chosen row, SET = next row, main dial = up/down rows,
 *    INFO / MENU = save & close, half-shutter = close and shoot.
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

#include "films.h"
#include "xmp.h"

#define VERSION "v1.0"
#define LOG_PATH      "ML/LOGS/FILMLOOK.LOG"
#define SETTINGS_PATH "ML/SETTINGS/FILMLOOK.TXT"
#define FL_CHOICES (FILM_COUNT + 1)          /* 0 = Off, 1.. = films */

extern int gui_state;
extern int lv;

/* ------------------------------------------------------------------ state */
static int st_film = 0;                   /* index into choices: 0 = Off */
static int st_strength = 100;             /* 0..100 */
static int st_grain = 100;                /* 0..200 */
static int st_contrast = 0;               /* -50..50 */

static int is_open = 0;
static int row = 0;                       /* 0 film, 1 effect, 2 grain, 3 contrast */
static int last_input_ms = 0;
static int key_log_count = 0;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

/* ------------------------------------------------------------------ settings file */
static void save_state(void)
{
    char t[96];
    snprintf(t, sizeof(t), "film=%d\nstrength=%d\ngrain=%d\ncontrast=%d\n",
             st_film, st_strength, st_grain, st_contrast);
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

static void load_state(void)
{
    static char buf[256];
    int n = read_file(SETTINGS_PATH, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        parse_after(buf, "film", &st_film);
        parse_after(buf, "strength", &st_strength);
        parse_after(buf, "grain", &st_grain);
        parse_after(buf, "contrast", &st_contrast);
    }
    st_film = clampi(st_film, 0, FL_CHOICES - 1);
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

static void draw_bar(int x, int y, int w, int v, int lo, int hi, int color)
{
    int fw = ((v - lo) * w) / (hi - lo);
    bmp_fill(C_DIM, x, y + 8, w, 4);
    if (fw > 0) bmp_fill(color, x, y + 8, fw, 4);
    bmp_fill(color, x + fw - 4, y, 8, 20);
}

static void draw_screen(void)
{
    int i, y;
    const film_t *f = (st_film > 0) ? &films[st_film - 1] : 0;
    char tmp[48];

    bmp_fill(C_BG, 0, 0, 720, 480);
    bmp_fill(C_ACCENT, 0, 0, 720, 6);

    bmp_printf(FONT(FONT_MED, C_ACCENT, C_BG), 40, 24, "FILMLOOK");
    bmp_printf(FONT(FONT_MED, C_DIM, C_BG), 600, 24, VERSION);
    bmp_fill(C_DIM, 40, 66, 640, 2);

    /* film selector */
    {
        int sel = (row == 0);
        int ac = sel ? C_ACCENT : C_DIM;
        bmp_printf(FONT(FONT_LARGE, ac, C_BG), 40, 90, "<");
        bmp_printf(FONT(FONT_LARGE, ac, C_BG), 664, 90, ">");
    }
    if (f) {
        bmp_printf(FONT(FONT_LARGE, C_TEXT, C_BG), 92, 90, "%s", f->name);
        bmp_printf(FONT(FONT_MED, C_ACCENT, C_BG), 92, 138, "%s   |   ISO %d", f->kind, f->iso);
        bmp_printf(FONT(FONT_MED, C_SOFT, C_BG), 92, 168, "%s", f->desc);
    } else {
        bmp_printf(FONT(FONT_LARGE, C_TEXT, C_BG), 92, 90, "Off");
        bmp_printf(FONT(FONT_MED, C_ACCENT, C_BG), 92, 138, "No film effect");
        bmp_printf(FONT(FONT_MED, C_SOFT, C_BG), 92, 168, "Photos are left alone, no sidecar written");
    }
    for (i = 0; i < FL_CHOICES; i++) {
        int x = 92 + i * 28;
        if (i == st_film) bmp_fill(C_ACCENT, x, 208, 16, 16);
        else              bmp_fill(C_DIM, x, 208, 16, 16);
    }
    bmp_fill(C_DIM, 40, 244, 640, 2);

    /* settings rows */
    {
        static const char *names[3] = { "Effect", "Grain", "Contrast" };
        for (i = 0; i < 3; i++) {
            int r = i + 1;
            int on = (row == r);
            int c = on ? C_ACCENT : C_TEXT;
            y = 264 + i * 50;
            if (on) bmp_fill(C_ACCENT, 40, y + 2, 4, 28);
            bmp_printf(FONT(FONT_MED_LARGE, c, C_BG), 64, y, "%s", names[i]);
            if (i == 0) { draw_bar(252, y + 4, 300, st_strength, 0, 100, c);
                          snprintf(tmp, sizeof(tmp), "%d %%", st_strength); }
            else if (i == 1) { draw_bar(252, y + 4, 300, st_grain, 0, 200, c);
                          snprintf(tmp, sizeof(tmp), "%d %%", st_grain); }
            else { draw_bar(252, y + 4, 300, st_contrast, -50, 50, c);
                   snprintf(tmp, sizeof(tmp), "%s%d", st_contrast >= 0 ? "+" : "", st_contrast); }
            bmp_printf(FONT(FONT_MED_LARGE, c, C_BG), 592, y, "%s", tmp);
        }
    }

    bmp_fill(C_DIM, 40, 424, 640, 2);
    bmp_printf(FONT(FONT_MED, C_SOFT, C_BG), 40, 440, "rear wheel: change    SET: next row    INFO: close");
}

/* ------------------------------------------------------------------ open / close */
static void open_screen(void)
{
    is_open = 1;
    row = 0;
    last_input_ms = get_ms_clock();
    canon_gui_disable_front_buffer();
    draw_screen();
    flog("screen opened, film", st_film);
}

static void close_screen(void)
{
    if (!is_open) return;
    is_open = 0;
    save_state();
    flog("screen closed, film", st_film);
    canon_gui_enable_front_buffer(1);
}

static void change_value(int dir)
{
    if (row == 0)      st_film = clampi(st_film + dir, 0, FL_CHOICES - 1);
    else if (row == 1) st_strength = clampi(st_strength + dir * 5, 0, 100);
    else if (row == 2) st_grain = clampi(st_grain + dir * 5, 0, 200);
    else               st_contrast = clampi(st_contrast + dir * 5, -50, 50);
}

/* ------------------------------------------------------------------ keys */
static int idle_photo_mode(void)
{
    return gui_state == GUISTATE_IDLE && !lv;
}

static unsigned int filmlook_keypress(unsigned int key)
{
    if (key_log_count < 300) { key_log_count++; flog("key", key); }

    if (!is_open) {
        if (key == MODULE_KEY_INFO && idle_photo_mode()) { open_screen(); return 0; }
        if (key == MODULE_KEY_TRASH && idle_photo_mode()) return 0;   /* keeps ML's big menu closed */
        return 1;
    }

    last_input_ms = get_ms_clock();
    switch (key) {
    case MODULE_KEY_WHEEL_DOWN:  change_value(+1); break;   /* rear wheel right */
    case MODULE_KEY_WHEEL_UP:    change_value(-1); break;   /* rear wheel left  */
    case MODULE_KEY_PRESS_RIGHT: change_value(+1); break;   /* joystick, if the camera sends it */
    case MODULE_KEY_PRESS_LEFT:  change_value(-1); break;
    case MODULE_KEY_WHEEL_RIGHT: row = clampi(row + 1, 0, 3); break;  /* main dial */
    case MODULE_KEY_WHEEL_LEFT:  row = clampi(row - 1, 0, 3); break;
    case MODULE_KEY_PRESS_DOWN:  row = clampi(row + 1, 0, 3); break;
    case MODULE_KEY_PRESS_UP:    row = clampi(row - 1, 0, 3); break;
    case MODULE_KEY_PRESS_SET:
    case MODULE_KEY_JOY_CENTER:  row = (row + 1) % 4; break;
    case MODULE_KEY_INFO:
    case MODULE_KEY_MENU:        close_screen(); return 0;
    case MODULE_KEY_PRESS_HALFSHUTTER:
    case MODULE_KEY_PLAY:        close_screen(); return 1;          /* pass on to the camera */
    default: return 0;                                              /* ignore everything else */
    }
    draw_screen();
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
            flog_s("photo queued", films[st_film - 1].name);
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
    static char xmp[4096];
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
    if (is_open && (t - last_input_ms) > 45000) { flog("auto-close (idle)", 0); close_screen(); }
    for (i = 0; i < QMAX; i++)
        if (queue[i].used && (t - queue[i].t) >= 3000)       /* let the camera finish writing the photo */
            write_sidecar(&queue[i]);
    return 0;
}

/* ------------------------------------------------------------------ module glue */
static unsigned int filmlook_init(void)
{
    load_state();
    flog_s("FILMLOOK module loaded", VERSION);
    flog("film", st_film);
    return 0;
}

static unsigned int filmlook_deinit(void) { return 0; }

MODULE_INFO_START()
    MODULE_INIT(filmlook_init)
    MODULE_DEINIT(filmlook_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_KEYPRESS, filmlook_keypress, 0)
    MODULE_CBR(CBR_SECONDS_CLOCK, filmlook_seconds, 0)
MODULE_CBRS_END()

MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_FILE_NUMBER_A)
MODULE_PROPHANDLERS_END()
