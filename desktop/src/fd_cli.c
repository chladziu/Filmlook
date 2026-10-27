/* fd_cli.c - FILM LOOK DESKTOP, the command line version (Windows, macOS, Linux).
 *
 * Same engine as the camera module and as the page in the browser (see fd_core.h). Photos can be dropped onto the program
 * (Windows) or given on the command line; with no film given on the command line the program asks for it.
 *
 *   filmlook_cli [options] photo_or_folder ...
 *     --film NAME|NUMBER     the film (see --list); asked for when missing
 *     --size L|M|S           5616x3744 (default), 3861x2574, 2784x1856
 *     --quality N            JPEG quality, default 95
 *     --out FOLDER           where the JPGs go; default: the folder FILMLOOK next to the photo
 *     --efekt N --ziarno N --kontrast N --matowosc N --cieplo N --cienie N --swiatla N --nasycenie N --winieta N
 *     --list                 the films
 *     --scale N              test: the engine's own 1/N size (N = 1, 2, 4) instead of --size
 *     --quiet                no progress output
 *
 * Own code of the film look project. */
#include "fd_core.h"
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#  include <wchar.h>
#  include <windows.h>
#  include <direct.h>
#  include <shellapi.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------ paths: UTF-8 inside, the system's way outside */
#ifdef _WIN32
static wchar_t *u2w(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, 0, 0);
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)(n > 0 ? n : 1));
    if (!w) return 0;
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n); else w[0] = 0;
    return w;
}
static char *w2u(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    char *s = (char *)malloc((size_t)(n > 0 ? n : 1));
    if (!s) return 0;
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, 0, 0); else s[0] = 0;
    return s;
}
static FILE *u_fopen(const char *p, const char *m)
{
    wchar_t *w = u2w(p), *wm = u2w(m);
    FILE *f = (w && wm) ? _wfopen(w, wm) : 0;
    free(w); free(wm);
    return f;
}
static int u_mkdir(const char *p) { wchar_t *w = u2w(p); int r = w ? _wmkdir(w) : -1; free(w); return r; }
static int u_isdir(const char *p)
{
    wchar_t *w = u2w(p); DWORD a = w ? GetFileAttributesW(w) : INVALID_FILE_ATTRIBUTES; free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
#define PSEP '\\'
#else
static FILE *u_fopen(const char *p, const char *m) { return fopen(p, m); }
static int u_mkdir(const char *p) { return mkdir(p, 0777); }
static int u_isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
#define PSEP '/'
#endif

static int is_sep(char c) { return c == '/' || c == '\\'; }

/* ------------------------------------------------------------ a simple growing list of file names */
typedef struct { char **v; int n, cap; } list_t;
static void list_add(list_t *l, const char *s)
{
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->v = (char **)realloc(l->v, sizeof(char *) * (size_t)l->cap); }
    l->v[l->n] = (char *)malloc(strlen(s) + 1);
    strcpy(l->v[l->n++], s);
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int ext_is_photo(const char *name)
{
    const char *d = strrchr(name, '.');
    char e[8]; int i;
    if (!d || strlen(d) > 5) return 0;
    for (i = 0; d[i + 1] && i < 6; i++) e[i] = (char)tolower((unsigned char)d[i + 1]);
    e[i] = 0;
    return !strcmp(e, "jpg") || !strcmp(e, "jpeg") || !strcmp(e, "cr2");
}

/* the photos of a folder (not the sub folders), sorted */
static void list_folder(const char *dir, list_t *out)
{
    list_t tmp = { 0, 0, 0 };
    int i;
#ifdef _WIN32
    wchar_t *wd = u2w(dir), *pat;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    size_t n;
    if (!wd) return;
    n = wcslen(wd);
    pat = (wchar_t *)malloc(sizeof(wchar_t) * (n + 3));
    wcscpy(pat, wd); wcscat(pat, L"\\*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char *u = w2u(fd.cFileName);
                if (u && ext_is_photo(u)) list_add(&tmp, u);
                free(u);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    free(pat); free(wd);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d)) != 0) if (ext_is_photo(e->d_name)) list_add(&tmp, e->d_name);
    closedir(d);
#endif
    if (tmp.n > 1) qsort(tmp.v, (size_t)tmp.n, sizeof(char *), cmp_str);
    for (i = 0; i < tmp.n; i++) {
        size_t l = strlen(dir) + strlen(tmp.v[i]) + 2;
        char *full = (char *)malloc(l);
        if (strlen(dir) && !is_sep(dir[strlen(dir) - 1])) snprintf(full, l, "%s%c%s", dir, PSEP, tmp.v[i]);
        else snprintf(full, l, "%s%s", dir, tmp.v[i]);
        list_add(out, full);
        free(full); free(tmp.v[i]);
    }
    free(tmp.v);
}

/* ------------------------------------------------------------ names */
static void base_of(const char *path, char *dst, size_t cap, char *dir, size_t dcap)      /* name without extension, and the folder */
{
    const char *b = path, *p, *dot;
    size_t n;
    for (p = path; *p; p++) if (is_sep(*p)) b = p + 1;
    n = (size_t)(b - path);
    if (dir) { if (n >= dcap) n = dcap - 1; memcpy(dir, path, n); dir[n] = 0; }
    dot = strrchr(b, '.');
    n = dot ? (size_t)(dot - b) : strlen(b);
    if (n >= cap) n = cap - 1;
    memcpy(dst, b, n); dst[n] = 0;
}

static void slug(const char *s, char *dst, size_t cap)        /* "Kodak Portra 400" -> "Kodak_Portra_400" */
{
    size_t n = 0; int last_us = 0;
    for (; *s && n + 1 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c)) { dst[n++] = (char)c; last_us = 0; }
        else if (!last_us && n) { dst[n++] = '_'; last_us = 1; }
    }
    while (n && dst[n - 1] == '_') n--;
    dst[n] = 0;
}

/* ------------------------------------------------------------ the run */
static int g_quiet = 0, g_last_pct = -1;
static void on_progress(void *ctx, int pm)
{
    (void)ctx;
    if (!g_quiet && pm / 10 != g_last_pct) { g_last_pct = pm / 10; fprintf(stderr, "\r  %3d %%", g_last_pct); fflush(stderr); }
}

static int read_file(const char *path, uint8_t **data, size_t *n)
{
    FILE *f = u_fopen(path, "rb");
    long sz;
    if (!f) return 1;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 1; }
    *data = (uint8_t *)malloc((size_t)sz);
    if (!*data || fread(*data, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(*data); *data = 0; return 1; }
    fclose(f);
    *n = (size_t)sz;
    return 0;
}

/* 0 = ok. outdir NULL = the folder FILMLOOK next to the photo. outpath (when not NULL) gets the file written. */
static int process_one(const char *path, const char *outdir, const fd_job *job, char *outpath, size_t ocap)
{
    uint8_t *src = 0; size_t sn = 0;
    fd_buf out; fd_result r;
    char base[512], dir[1024], film[128], od[1100];
    FILE *f;
    clock_t t0 = clock();

    if (read_file(path, &src, &sn)) { fprintf(stderr, "  nie można odczytać pliku\n"); return 1; }
    g_last_pct = -1;
    fd_run(src, sn, job, &out, &r, on_progress, 0);
    free(src);
    if (!g_quiet) fprintf(stderr, "\r        \r");
    if (r.rc != 0) { fprintf(stderr, "  BŁĄD: %s\n", fd_error_text(r.rc)); return 1; }
    base_of(path, base, sizeof base, dir, sizeof dir);
    slug(fd_film_text(job->film, 1), film, sizeof film);
    if (outdir) snprintf(od, sizeof od, "%s", outdir);
    else snprintf(od, sizeof od, "%sFILMLOOK", dir);
    u_mkdir(od);
    snprintf(outpath, ocap, "%s%c%s_%s%s.jpg", od, PSEP, base, film,
             job->size == FD_SIZE_M ? "_M" : (job->size == FD_SIZE_S ? "_S" : ""));
    f = u_fopen(outpath, "wb");
    if (!f || fwrite(out.data, 1, out.len, f) != out.len) { fprintf(stderr, "  nie można zapisać %s\n", outpath); if (f) fclose(f); free(out.data); return 1; }
    fclose(f);
    free(out.data);
    if (!g_quiet) fprintf(stderr, "  %dx%d -> %dx%d  %.1f s  %s\n", r.src_w, r.src_h, r.out_w, r.out_h,
                          (double)(clock() - t0) / CLOCKS_PER_SEC, outpath);
    return 0;
}

/* ------------------------------------------------------------ the command line */
static int find_film(const char *s)
{
    int i;
    char *end;
    long v = strtol(s, &end, 10);
    if (*s && !*end) return (v >= 1 && v <= FILM_COUNT) ? (int)v - 1 : -1;
    for (i = 0; i < FILM_COUNT; i++) if (!strcmp(fd_film_text(i, 1), s) || !strcmp(fd_film_text(i, 0), s)) return i;
    return -1;
}

static void print_films(void)
{
    int i;
    for (i = 0; i < FILM_COUNT; i++)
        fprintf(stderr, "  %2d  %-28s %s\n", i + 1, fd_film_text(i, 1), fd_film_text(i, 3));
}

static int ask(const char *question, char *buf, size_t cap)
{
    fprintf(stderr, "%s", question); fflush(stderr);
    if (!fgets(buf, (int)cap, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = 0;
    return 1;
}

static void usage(void)
{
    fprintf(stderr,
        "FILM LOOK DESKTOP %s - film z aparatu na komputerze\n"
        "Użycie: filmlook_cli [opcje] zdjęcie_lub_folder ...\n"
        "  --film NAZWA|NUMER   film (lista: --list); jeśli go nie ma, program zapyta\n"
        "  --size L|M|S         5616x3744 (domyślnie), 3861x2574, 2784x1856\n"
        "  --quality N          jakość JPG, domyślnie 95\n"
        "  --out FOLDER         dokąd zapisać (domyślnie folder FILMLOOK obok zdjęcia)\n"
        "  --efekt --ziarno --kontrast --matowosc --cieplo --cienie --swiatla --nasycenie --winieta  WARTOŚĆ\n"
        "  --expo --hi_t --sh_t --wh_t --bl_t --gsize --grough --wb_temp --wb_tint --skin_hue --skin_sat --skin_lum\n"
        "  --ton_mode --cg_sh_h --cg_sh_s --cg_mid_h --cg_mid_s --cg_hi_h --cg_hi_s --filter --filter_amt\n"
        "  --hue_KOLOR --sat_KOLOR --lum_KOLOR  (KOLOR: red orange yellow green aqua blue purple magenta)  WARTOŚĆ\n"
        "  --list               pokaż filmy\n", FD_VERSION);
}

int fd_main(int argc, char **argv)
{
    fd_job job;
    list_t files = { 0, 0, 0 };
    const char *outdir = 0, *filmarg = 0;
    int i, ok = 0, bad = 0, interactive_needed = 0, scale = 0, size_set = 0;
    char buf[256], outpath[2048];

    fd_job_defaults(&job);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--list")) { print_films(); return 0; }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--quiet")) g_quiet = 1;
        else if (i + 1 < argc && !strcmp(a, "--film")) filmarg = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--out")) outdir = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--quality")) job.quality = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--scale")) scale = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--size")) {
            char c = (char)tolower((unsigned char)argv[++i][0]);
            job.size = c == 'm' ? FD_SIZE_M : (c == 's' ? FD_SIZE_S : FD_SIZE_FULL); size_set = 1;
        }
        else if (i + 1 < argc && !strcmp(a, "--efekt")) job.look.strength = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--ziarno")) job.look.grain = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--kontrast")) job.look.contrast = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--matowosc")) job.look.fade = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--cieplo")) job.look.warm = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--cienie")) job.look.shadow = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--swiatla")) job.look.high = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--nasycenie")) job.look.sat = atoi(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--winieta")) job.look.vig = atoi(argv[++i]);
        else if (i + 1 < argc && a[0] == '-' && a[1] == '-' && fd_job_set_name(&job, a + 2, atoi(argv[i + 1]))) i++;   /* --expo 50, --wb_temp -20, --hue_orange 10 ... */
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "nieznana opcja %s\n", a); usage(); return 2; }
        else if (u_isdir(a)) list_folder(a, &files);
        else list_add(&files, a);
    }
    if (files.n == 0) { usage(); interactive_needed = 1; }

    if (scale) { job.size = FD_SIZE_ENGINE + scale; size_set = 1; }
    if (files.n > 0 && !filmarg) {                               /* dropped on the program: ask what to do */
        interactive_needed = 1;
        fprintf(stderr, "\nFilmy:\n"); print_films();
        for (;;) {
            if (!ask("\nNumer filmu: ", buf, sizeof buf)) return 2;
            job.film = find_film(buf);
            if (job.film >= 0) break;
            fprintf(stderr, "Nie ma takiego filmu.\n");
        }
        if (!size_set) {
            if (ask("Rozmiar: L = 5616x3744, M = 3861x2574, S = 2784x1856 [L]: ", buf, sizeof buf) && buf[0]) {
                char c = (char)tolower((unsigned char)buf[0]);
                job.size = c == 'm' ? FD_SIZE_M : (c == 's' ? FD_SIZE_S : FD_SIZE_FULL);
            }
        }
    } else if (filmarg) {
        job.film = find_film(filmarg);
        if (job.film < 0) { fprintf(stderr, "Nie ma filmu \"%s\" (lista: --list)\n", filmarg); return 2; }
    }

    for (i = 0; i < files.n; i++) {
        if (!g_quiet) fprintf(stderr, "[%d/%d] %s\n", i + 1, files.n, files.v[i]);
        if (process_one(files.v[i], outdir, &job, outpath, sizeof outpath) == 0) ok++; else bad++;
    }
    if (files.n > 0) {
        fprintf(stderr, "\nGotowe: %d zdjęć", ok);
        if (bad) fprintf(stderr, ", nie udało się: %d", bad);
        fprintf(stderr, ".\n");
    }
    if (interactive_needed) { ask("Naciśnij Enter, aby zamknąć. ", buf, sizeof buf); }
    return bad ? 1 : 0;
}

#ifdef _WIN32
int main(void)
{
    int argc = 0, i, rc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv = (char **)malloc(sizeof(char *) * (size_t)(argc + 1));
    SetConsoleOutputCP(CP_UTF8);
    for (i = 0; i < argc; i++) argv[i] = w2u(wargv[i]);
    argv[argc] = 0;
    rc = fd_main(argc, argv);
    return rc;
}
#else
int main(int argc, char **argv) { return fd_main(argc, argv); }
#endif
