/* fd_wasm.c - FILM LOOK DESKTOP, the browser version: the engine compiled to WebAssembly (clang --target=wasm32, no emscripten).
 *
 * There is no C library in the browser, so the few functions the engine needs (memcpy, memset, ...) and a very small
 * memory allocator are written here. The allocator only moves a pointer forward; fd_reset() takes everything back at once,
 * which is what the page does after every photo. The page (web/page.html) talks to the functions exported below.
 *
 * Own code of the film look project. */
#define FD_WASM 1
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "fd_core.h"

/* ------------------------------------------------------------ the C library that the engine needs
 * Plain loops (no_builtin: the compiler must not turn them back into calls of themselves). The wasm is built for the oldest
 * WebAssembly (-mcpu=mvp), so that every browser of the last years runs it. */
#define NOBUILTIN __attribute__((no_builtin))
NOBUILTIN void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    while (n >= 8) { dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3]; dp[4] = sp[4]; dp[5] = sp[5]; dp[6] = sp[6]; dp[7] = sp[7]; dp += 8; sp += 8; n -= 8; }
    while (n--) *dp++ = *sp++;
    return d;
}
NOBUILTIN void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = (uint8_t *)d; const uint8_t *sp = (const uint8_t *)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp || dp >= sp + n) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
NOBUILTIN void *memset(void *d, int c, size_t n)
{
    uint8_t *dp = (uint8_t *)d; uint8_t v = (uint8_t)c;
    while (n >= 8) { dp[0] = v; dp[1] = v; dp[2] = v; dp[3] = v; dp[4] = v; dp[5] = v; dp[6] = v; dp[7] = v; dp += 8; n -= 8; }
    while (n--) *dp++ = v;
    return d;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}
int vsnprintf(char *b, size_t n, const char *f, va_list ap) { (void)f; (void)ap; if (n) b[0] = 0; return 0; }   /* the engine never logs here */

/* ------------------------------------------------------------ memory: a stack-like heap on top of the wasm memory */
extern unsigned char __heap_base;
typedef struct { uint32_t size; uint32_t prev; uint32_t pad[2]; } blk_t;       /* 16 bytes in front of every block */
static uintptr_t g_base, g_top, g_last;                                          /* start of the heap, its end, the last block */
static uintptr_t g_mark_top, g_mark_last;

static int heap_ensure(uintptr_t end)
{
    size_t have = (size_t)__builtin_wasm_memory_size(0) * 65536u;
    size_t need;
    if (end <= have) return 1;
    need = (end - have + 65535u) / 65536u;
    return __builtin_wasm_memory_grow(0, need) != (size_t)-1;
}

void *malloc(size_t n)
{
    uintptr_t h, p, end;
    size_t sz = (n + 15u) & ~(size_t)15u;
    if (!g_base) { g_base = ((uintptr_t)&__heap_base + 15u) & ~(uintptr_t)15u; g_top = g_base; g_last = 0; }
    h = g_top; p = h + 16; end = p + sz;
    if (sz < n || end < p || !heap_ensure(end)) return 0;
    ((blk_t *)h)->size = (uint32_t)sz; ((blk_t *)h)->prev = (uint32_t)g_last;
    g_last = h; g_top = end;
    return (void *)p;
}

void free(void *p)               /* only the newest block gives its space back; the rest comes back with fd_reset() */
{
    uintptr_t h;
    if (!p) return;
    h = (uintptr_t)p - 16;
    if (h == g_last) { g_last = ((blk_t *)h)->prev; g_top = h; }
}

void *realloc(void *p, size_t n)
{
    uintptr_t h;
    size_t old, sz;
    void *q;
    if (!p) return malloc(n);
    h = (uintptr_t)p - 16;
    old = ((blk_t *)h)->size; sz = (n + 15u) & ~(size_t)15u;
    if (sz < n) return 0;
    if (sz <= old) return p;
    if (h == g_last) {                                                          /* the newest block grows in place */
        uintptr_t end = h + 16 + sz;
        if (!heap_ensure(end)) return 0;
        ((blk_t *)h)->size = (uint32_t)sz; g_top = end;
        return p;
    }
    q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, old);
    return q;
}

#define EXPORT(name) __attribute__((export_name(name)))

EXPORT("fd_alloc") void *fd_alloc(size_t n) { return malloc(n); }
EXPORT("fd_reset") void fd_reset(void) { g_top = g_base ? g_base : 0; g_last = 0; g_mark_top = 0; g_mark_last = 0; }
EXPORT("fd_heap_mark") void fd_heap_mark(void) { g_mark_top = g_top; g_mark_last = g_last; }      /* what is allocated now stays ... */
EXPORT("fd_heap_release") void fd_heap_release(void) { if (g_mark_top) { g_top = g_mark_top; g_last = g_mark_last; } }   /* ... when everything after it goes */

/* ------------------------------------------------------------ the films */
EXPORT("fd_version") const char *wasm_version(void) { return FD_VERSION; }
EXPORT("fd_film_count") int wasm_film_count(void) { return fd_film_count(); }
EXPORT("fd_film_text") const char *wasm_film_text(int i, int which) { return fd_film_text(i, which); }
EXPORT("fd_film_num") int wasm_film_num(int i, int which) { return fd_film_num(i, which); }
EXPORT("fd_error_text") const char *wasm_error_text(int rc) { return fd_error_text(rc); }

/* ------------------------------------------------------------ one photo */
__attribute__((import_module("env"), import_name("progress"))) extern void js_progress(int permille);

static fd_buf g_out;
static fd_result g_res;
static int g_last_pm;

static void on_progress(void *ctx, int pm)
{
    (void)ctx;
    if (pm / 5 != g_last_pm) { g_last_pm = pm / 5; js_progress(pm); }       /* 0.5 % steps are plenty for a bar */
}

/* size: 0 5616x3744, 1 3861x2574, 2 2784x1856, 101 / 102 the engine's own full / half size (the preview on the page uses 102).
 * Returns 0 or an error code (see fd_error_text). */
EXPORT("fd_process")
int fd_process(const uint8_t *src, size_t n, int film, int size, int quality,
               int strength, int grain, int contrast, int fade, int warm, int shadow, int high, int sat, int vig)
{
    fd_job j;
    fd_job_defaults(&j);
    j.film = film; j.size = size; j.quality = quality;
    j.look.strength = strength; j.look.grain = grain; j.look.contrast = contrast; j.look.fade = fade;
    j.look.warm = warm; j.look.shadow = shadow; j.look.high = high; j.look.sat = sat; j.look.vig = vig;
    g_last_pm = -1;
    fd_run(src, n, &j, &g_out, &g_res, on_progress, 0);
    return g_res.rc;
}

/* after fd_process: 0 result code, 1 source width, 2 source height, 3 output width, 4 output height, 5 JPEG size in bytes,
 * 6 address of the JPEG in the wasm memory */
EXPORT("fd_result_int")
unsigned fd_result_int(int k)
{
    switch (k) {
    case 0: return (unsigned)g_res.rc;
    case 1: return (unsigned)g_res.src_w;
    case 2: return (unsigned)g_res.src_h;
    case 3: return (unsigned)g_res.out_w;
    case 4: return (unsigned)g_res.out_h;
    case 5: return (unsigned)g_out.len;
    case 6: return (unsigned)(uintptr_t)g_out.data;
    default: return 0;
    }
}
