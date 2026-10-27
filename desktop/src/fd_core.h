/* fd_core.h - FILM LOOK DESKTOP: the glue between this program and the film engine of the module (modules/filmlook).
 *
 * The engine is used exactly as the camera module uses it: develop.h (fl_develop) reads the JPEG of the camera (or the
 * full size preview inside a CR2), grades it with a film (films.h), and writes a new JPEG. This file adds only what a
 * computer program needs on top:
 *   - the whole photo is in memory (a file read in one go, the result is collected in a buffer),
 *   - the three output sizes of the 5D Mark II (5616x3744, 3861x2574, 2784x1856),
 *   - readable error texts (Polish), and a list of the films for the screen.
 * Nothing under modules/filmlook is copied or changed: later improvements of the engine come in with a rebuild.
 * The only place that calls the engine is fd_run() below; if its interface changes, this is the place to adjust.
 *
 * Own code of the film look project. The engine files that get linked in are Soup Engine (GPL v2): see LICENSE. */
#ifndef FD_CORE_H
#define FD_CORE_H

#ifdef FD_WASM                                   /* the browser build: no C library at all (see fd_wasm.c) */
void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
#else
#  ifndef SE_HOSTED
#    define SE_HOSTED 1
#  endif
#  include <stdlib.h>
#  include <stdio.h>
#endif
#include <stdint.h>
#include <stddef.h>

#include "../../modules/filmlook/develop.h"
#include "fd_resize.h"
#include "fd_containers.h"

#define FD_VERSION "0.1"

/* ---------------------------------------------------------------- sizes */
#define FD_SIZE_FULL   0        /* 5616 x 3744 (L): the picture as it is */
#define FD_SIZE_M      1        /* 3861 x 2574 (M) */
#define FD_SIZE_S      2        /* 2784 x 1856 (S) */
#define FD_SIZE_COUNT  3
#define FD_SIZE_ENGINE 100      /* + 1, 2 or 4: the decoder's own 1/1, 1/2 or 1/4 size (preview on the screen, tests) */

static const struct { int w, h; } fd_sizes[FD_SIZE_COUNT] = { { 5616, 3744 }, { 3861, 2574 }, { 2784, 1856 } };

/* the size a picture of sw x sh comes out at: the long side of the chosen size, the short side in proportion.
 * A picture is never enlarged: when it is not bigger than the chosen size it stays as it is. */
static void fd_target(int sw, int sh, int size, int *ow, int *oh)
{
    int ls = sw > sh ? sw : sh, ss = sw > sh ? sh : sw, lt, st;
    *ow = sw; *oh = sh;
    if (size <= FD_SIZE_FULL || size >= FD_SIZE_COUNT) return;
    lt = fd_sizes[size].w;
    if (lt >= ls) return;
    st = (int)(((int64_t)ss * lt + ls / 2) / ls);
    if (st < 1) st = 1;
    if (sw >= sh) { *ow = lt; *oh = st; } else { *ow = st; *oh = lt; }
}

/* ---------------------------------------------------------------- films */
static int fd_film_count(void) { return FILM_COUNT; }
/* which: 0 name on the screen, 1 full name (brand + name), 2 maker, 3 type (Kolor / Czarno-biały / Slajd),
 *        4 kind (one line), 5 description; the texts are Polish */
static const char *fd_film_text(int i, int which)
{
    const film_t *f;
    if (i < 0 || i >= FILM_COUNT) return "";
    f = &films[i];
    switch (which) {
    case 0: return f->name;
    case 1: return f->full;
    case 2: return groups[f->group].brand;
    case 3: return types[groups[f->group].type].name;
    case 4: return f->kind[0];
    case 5: return f->desc[0];
    default: return "";
    }
}
/* which: 0 type number, 1 ISO (0 = none), 2 black and white (1) or colour (0) */
static int fd_film_num(int i, int which)
{
    if (i < 0 || i >= FILM_COUNT) return 0;
    switch (which) {
    case 0: return groups[films[i].group].type;
    case 1: return films[i].iso;
    case 2: return films[i].mono ? 1 : 0;
    default: return 0;
    }
}

/* ---------------------------------------------------------------- errors */
#define FD_E_FILM     -100
#define FD_E_SIZE     -101
static const char *fd_error_text(int rc)
{
    switch (rc) {
    case 0:                return "gotowe";
    case SE_E_BADFILE:     return "to nie jest zdjęcie JPG, CR2, CR3 ani DNG z aparatu";
    case SE_E_UNSUPPORTED: return "nieobsługiwany plik: progresywny JPG albo RAW (CR2, CR3, DNG) bez dużego podglądu JPG w środku (DNG zapisz z „pełnowymiarowym podglądem JPEG”)";
    case SE_E_TRUNCATED:   return "plik jest ucięty";
    case SE_E_NOMEM:       return "za mało pamięci na to zdjęcie";
    case SE_E_CORRUPT:     return "uszkodzone dane w pliku";
    case SE_E_IO:          return "błąd odczytu lub zapisu";
    case FD_E_FILM:        return "nie ma takiego filmu";
    case FD_E_SIZE:        return "nie udało się zmienić rozmiaru";
    default:               return "nieznany błąd";
    }
}

/* ---------------------------------------------------------------- a job */
typedef struct {
    int film;               /* index into films[] */
    int size;               /* FD_SIZE_... */
    int quality;            /* JPEG quality of the result, 1..100 */
    look_t look;            /* Efekt, Ziarno, Kontrast, Matowość, Ciepło, Cienie, Światła, Nasycenie, Winieta (the panel's sliders) */
} fd_job;

static void fd_job_defaults(fd_job *j)          /* the defaults of every film in the panel: Efekt 100 %, Ziarno 100 %, the rest 0 */
{
    memset(j, 0, sizeof *j);
    j->size = FD_SIZE_FULL; j->quality = 95;
    j->look.strength = 100; j->look.grain = 100;
    j->look.filter_amt = 100;                   /* a colour filter, once chosen, works at full strength */
    j->look.gcolor = 100;                       /* the grain of a colour film is coloured (a black and white film ignores it) */
}

/* ---------------------------------------------------------------- the sliders of the desktop program
 * Every number of look_t that the camera does not have, by name: the page asks for the list (fd_param_name) and sends the
 * values by index, the command line takes them as --name value, the tests use both. Adding a slider = one line here. */
#define FDP(n, field) { n, (int)offsetof(look_t, field) }
static const struct { const char *name; int off; } fd_params[] = {
    FDP("expo", expo), FDP("hi_t", hi_t), FDP("sh_t", sh_t), FDP("wh_t", wh_t), FDP("bl_t", bl_t),
    FDP("gsize", gsize), FDP("grough", grough), FDP("gcolor", gcolor),
    FDP("wb_temp", wb_temp), FDP("wb_tint", wb_tint),
    FDP("skin_hue", skin_hue), FDP("skin_sat", skin_sat), FDP("skin_lum", skin_lum),
    FDP("ton_mode", ton_mode),
    FDP("cg_sh_h", cg[0]), FDP("cg_sh_s", cg[1]), FDP("cg_mid_h", cg[2]), FDP("cg_mid_s", cg[3]),
    FDP("cg_hi_h", cg[4]), FDP("cg_hi_s", cg[5]), FDP("cg_gl_h", cg[6]), FDP("cg_gl_s", cg[7]),
    FDP("filter", filter), FDP("filter_amt", filter_amt),
    FDP("hue_red", hsl[0]), FDP("hue_orange", hsl[1]), FDP("hue_yellow", hsl[2]), FDP("hue_green", hsl[3]),
    FDP("hue_aqua", hsl[4]), FDP("hue_blue", hsl[5]), FDP("hue_purple", hsl[6]), FDP("hue_magenta", hsl[7]),
    FDP("sat_red", hsl[8]), FDP("sat_orange", hsl[9]), FDP("sat_yellow", hsl[10]), FDP("sat_green", hsl[11]),
    FDP("sat_aqua", hsl[12]), FDP("sat_blue", hsl[13]), FDP("sat_purple", hsl[14]), FDP("sat_magenta", hsl[15]),
    FDP("lum_red", hsl[16]), FDP("lum_orange", hsl[17]), FDP("lum_yellow", hsl[18]), FDP("lum_green", hsl[19]),
    FDP("lum_aqua", hsl[20]), FDP("lum_blue", hsl[21]), FDP("lum_purple", hsl[22]), FDP("lum_magenta", hsl[23]),
    /* the curves the user draws: crvK_n points of channel K (0 master, 1 red, 2 green, 3 blue), crvK_xJ / crvK_yJ the points (0..255) */
    FDP("crv0_n", uc_n[0]), FDP("crv0_x0", uc_x[0][0]), FDP("crv0_y0", uc_y[0][0]), FDP("crv0_x1", uc_x[0][1]), FDP("crv0_y1", uc_y[0][1]), FDP("crv0_x2", uc_x[0][2]), FDP("crv0_y2", uc_y[0][2]), FDP("crv0_x3", uc_x[0][3]), FDP("crv0_y3", uc_y[0][3]), FDP("crv0_x4", uc_x[0][4]), FDP("crv0_y4", uc_y[0][4]), FDP("crv0_x5", uc_x[0][5]), FDP("crv0_y5", uc_y[0][5]), FDP("crv0_x6", uc_x[0][6]), FDP("crv0_y6", uc_y[0][6]), FDP("crv0_x7", uc_x[0][7]), FDP("crv0_y7", uc_y[0][7]), FDP("crv0_x8", uc_x[0][8]), FDP("crv0_y8", uc_y[0][8]), FDP("crv0_x9", uc_x[0][9]), FDP("crv0_y9", uc_y[0][9]), FDP("crv0_x10", uc_x[0][10]), FDP("crv0_y10", uc_y[0][10]), FDP("crv0_x11", uc_x[0][11]), FDP("crv0_y11", uc_y[0][11]),
    FDP("crv1_n", uc_n[1]), FDP("crv1_x0", uc_x[1][0]), FDP("crv1_y0", uc_y[1][0]), FDP("crv1_x1", uc_x[1][1]), FDP("crv1_y1", uc_y[1][1]), FDP("crv1_x2", uc_x[1][2]), FDP("crv1_y2", uc_y[1][2]), FDP("crv1_x3", uc_x[1][3]), FDP("crv1_y3", uc_y[1][3]), FDP("crv1_x4", uc_x[1][4]), FDP("crv1_y4", uc_y[1][4]), FDP("crv1_x5", uc_x[1][5]), FDP("crv1_y5", uc_y[1][5]), FDP("crv1_x6", uc_x[1][6]), FDP("crv1_y6", uc_y[1][6]), FDP("crv1_x7", uc_x[1][7]), FDP("crv1_y7", uc_y[1][7]), FDP("crv1_x8", uc_x[1][8]), FDP("crv1_y8", uc_y[1][8]), FDP("crv1_x9", uc_x[1][9]), FDP("crv1_y9", uc_y[1][9]), FDP("crv1_x10", uc_x[1][10]), FDP("crv1_y10", uc_y[1][10]), FDP("crv1_x11", uc_x[1][11]), FDP("crv1_y11", uc_y[1][11]),
    FDP("crv2_n", uc_n[2]), FDP("crv2_x0", uc_x[2][0]), FDP("crv2_y0", uc_y[2][0]), FDP("crv2_x1", uc_x[2][1]), FDP("crv2_y1", uc_y[2][1]), FDP("crv2_x2", uc_x[2][2]), FDP("crv2_y2", uc_y[2][2]), FDP("crv2_x3", uc_x[2][3]), FDP("crv2_y3", uc_y[2][3]), FDP("crv2_x4", uc_x[2][4]), FDP("crv2_y4", uc_y[2][4]), FDP("crv2_x5", uc_x[2][5]), FDP("crv2_y5", uc_y[2][5]), FDP("crv2_x6", uc_x[2][6]), FDP("crv2_y6", uc_y[2][6]), FDP("crv2_x7", uc_x[2][7]), FDP("crv2_y7", uc_y[2][7]), FDP("crv2_x8", uc_x[2][8]), FDP("crv2_y8", uc_y[2][8]), FDP("crv2_x9", uc_x[2][9]), FDP("crv2_y9", uc_y[2][9]), FDP("crv2_x10", uc_x[2][10]), FDP("crv2_y10", uc_y[2][10]), FDP("crv2_x11", uc_x[2][11]), FDP("crv2_y11", uc_y[2][11]),
    FDP("crv3_n", uc_n[3]), FDP("crv3_x0", uc_x[3][0]), FDP("crv3_y0", uc_y[3][0]), FDP("crv3_x1", uc_x[3][1]), FDP("crv3_y1", uc_y[3][1]), FDP("crv3_x2", uc_x[3][2]), FDP("crv3_y2", uc_y[3][2]), FDP("crv3_x3", uc_x[3][3]), FDP("crv3_y3", uc_y[3][3]), FDP("crv3_x4", uc_x[3][4]), FDP("crv3_y4", uc_y[3][4]), FDP("crv3_x5", uc_x[3][5]), FDP("crv3_y5", uc_y[3][5]), FDP("crv3_x6", uc_x[3][6]), FDP("crv3_y6", uc_y[3][6]), FDP("crv3_x7", uc_x[3][7]), FDP("crv3_y7", uc_y[3][7]), FDP("crv3_x8", uc_x[3][8]), FDP("crv3_y8", uc_y[3][8]), FDP("crv3_x9", uc_x[3][9]), FDP("crv3_y9", uc_y[3][9]), FDP("crv3_x10", uc_x[3][10]), FDP("crv3_y10", uc_y[3][10]), FDP("crv3_x11", uc_x[3][11]), FDP("crv3_y11", uc_y[3][11])
};
#define FD_PARAM_COUNT ((int)(sizeof fd_params / sizeof fd_params[0]))

static const char *fd_param_name(int k) { return (k >= 0 && k < FD_PARAM_COUNT) ? fd_params[k].name : ""; }
static void fd_job_set(fd_job *j, int k, int v)
{
    if (k >= 0 && k < FD_PARAM_COUNT) *(int *)((char *)&j->look + fd_params[k].off) = v;
}
/* by name; returns 1 when the name exists */
static int fd_job_set_name(fd_job *j, const char *name, int v)
{
    int k;
    for (k = 0; k < FD_PARAM_COUNT; k++) {
        const char *a = fd_params[k].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) { fd_job_set(j, k, v); return 1; }
    }
    return 0;
}

typedef struct { uint8_t *data; size_t len, cap; int fail; } fd_buf;     /* the JPEG that comes out */

typedef struct {
    int rc;                 /* 0 = ok, otherwise an se_err or FD_E_... */
    int src_w, src_h, out_w, out_h;
    size_t mem_peak;
} fd_result;

typedef struct { const uint8_t *p; size_t n; } fd_src;
static int fd_rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    const fd_src *s = (const fd_src *)ctx;
    if ((size_t)off + n > s->n) return 1;
    memcpy(dst, s->p + off, n);
    return 0;
}
static int fd_wr(void *ctx, const uint8_t *d, size_t n)
{
    fd_buf *b = (fd_buf *)ctx;
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : ((size_t)1 << 20);
        uint8_t *p;
        while (nc < b->len + n) nc += nc / 2;
        p = (uint8_t *)realloc(b->data, nc);
        if (!p) { b->fail = 1; return 1; }
        b->data = p; b->cap = nc;
    }
    memcpy(b->data + b->len, d, n);
    b->len += n;
    return 0;
}

/* ------------------------------------------------- smaller than the source
 * fl_develop() can deliver the whole picture or its 1/2 or 1/4 (the decoder makes it smaller while it decodes). The sizes
 * M and S of the 5D are in between, so for them the same pipeline is run with one more stage: decoder -> area average
 * (fd_resize.h) -> film look -> encoder. The function below is fl_develop() of develop.h with that stage added; the film look
 * runs on the finished small picture, like in the module's half size. Until the engine has these sizes itself, this is how
 * they are made; then the size cases in fd_run() can simply call the engine. */
static void fd_develop_sized(const fl_io *io, const film_t *f, const look_t *L, int size, int quality, fl_result *res)
{
    se_arena ar, lookar;
    se_look lk;
    se_jdec_info ji;
    se_jpeg_exif ex;
    fl_meta meta;
    fl_ctx c;
    fd_rs rs;
    uint8_t *hdr, *strip;
    uint32_t hlen, poff = 0, plen = 0;
    int ow, oh, gscale;
    se_err rc;

    memset(res, 0, sizeof *res);
    se_arena_init(&ar, io->mem, io->mem_size);
    hdr = SE_NEW(&ar, uint8_t, FL_HDR_BYTES);
    if (!hdr) { res->rc = SE_E_NOMEM; return; }
    hlen = io->src_size < FL_HDR_BYTES ? io->src_size : FL_HDR_BYTES;
    if (io->read(io->rctx, 0, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = fl_find_jpeg(hdr, hlen, io->src_size, &poff, &plen, &meta);
    if (rc != SE_OK) { res->rc = rc; return; }
    if (plen > io->src_size || poff > io->src_size - plen) { res->rc = SE_E_TRUNCATED; return; }
    hlen = plen < FL_HDR_BYTES ? plen : FL_HDR_BYTES;
    if (io->read(io->rctx, poff, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = se_jpeg_dec_info(hdr, hlen, &ji);
    if (rc != SE_OK) { res->rc = rc; return; }
    fd_target(ji.width, ji.height, size, &ow, &oh);
    res->src_w = ji.width; res->src_h = ji.height; res->out_w = ow; res->out_h = oh; res->scale = 1;
    gscale = (ji.width + ow / 2) / ow;               /* grain and halation are sized for a whole step 1, 2 ...: the nearest one */
    if (gscale < 1) gscale = 1;

    {
        size_t tabs = 3 * 1536 * sizeof(int16_t) + 64;
        size_t budget = SE_LOOK_BUDGET + (size_t)ow * 4 + 4096 + fl_look_extra(ow, L);
        uint8_t *mem = (uint8_t *)se_arena_alloc(&ar, tabs + budget, 8);
        if (!mem) { res->rc = SE_E_NOMEM; return; }
        se_arena_init(&lookar, mem, tabs + budget);
        if (fl_build_look(&lk, &lookar, f, L, gscale, oh) != 0) { res->rc = SE_E_NOMEM; return; }
        (void)se_look_fit(&lk, ow, budget);
        res->look_radius = lk.hal_amount > 0 ? lk.hal_radius : 0;
        rc = se_look_begin(&c.run, &lk, ow, &lookar);
        if (rc != SE_OK) { res->rc = rc; return; }
    }

    c.io = io; c.base = poff; c.ow = ow; c.err = 0; c.rows = 0; c.oh = oh > 0 ? oh : 1;
    c.enc = SE_NEW(&ar, se_jpeg, 1);
    strip = SE_NEW(&ar, uint8_t, SE_JPEG_STRIP_BYTES(ow));
    if (!c.enc || !strip) { res->rc = SE_E_NOMEM; return; }
    if (fd_rs_begin(&rs, ji.width, ji.height, ow, oh, fl_look_rows, &c) != 0) { res->rc = FD_E_SIZE; return; }

    memset(&ex, 0, sizeof ex);
    ex.make = meta.make[0] ? meta.make : "Canon";
    ex.model = meta.model[0] ? meta.model : "Canon EOS 5D Mark II";
    ex.software = f->full;
    ex.datetime = meta.dt;
    ex.exp_num = meta.en; ex.exp_den = meta.ed; ex.fn_num = meta.fn; ex.fn_den = meta.fd;
    ex.iso = meta.iso; ex.orientation = meta.orient;
    rc = se_jpeg_begin(c.enc, ow, oh, quality, strip, &ex, fl_enc_sink, &c);
    if (rc != SE_OK) { fd_rs_free(&rs); res->rc = rc; return; }

    rc = se_jpeg_decode_stream_scaled(hdr, hlen, plen, fl_src_read, &c, 1, &ar, fd_rs_rows, &rs, &ji);
    if (rc == SE_OK && (c.err || rs.failed)) rc = SE_E_IO;
    if (rc == SE_OK) rc = se_look_end(c.run, fl_look_emit, &c);
    if (rc == SE_OK && !c.err) rc = se_jpeg_end(c.enc);
    if (rc == SE_OK && c.err) rc = SE_E_IO;
    fd_rs_free(&rs);
    res->rc = rc;
    res->mem_peak = ar.peak;
}

/* ---------------------------------------------------------------- run
 * Develops one photo (src = the whole file: a camera JPG, a CR2, a CR3 or a DNG) into out. out->data is allocated here (free() it);
 * out->len is the size of the finished JPEG. Returns r->rc. progress (may be NULL) gets 0..1000. */
static int fd_run(const uint8_t *src0, size_t srclen0, const fd_job *job, fd_buf *out, fd_result *r,
                  void (*progress)(void *ctx, int permille), void *pctx)
{
    fd_src s;
    fl_io io;
    fl_result res;
    film_t f;
    const uint8_t *src = src0;
    size_t srclen = srclen0;
    uint8_t *owned = 0;
    int sz = job->size, quality = job->quality, prc = 0;
    int tw = 0, th = 0, sw = 0, sh = 0, resize = 0;

    memset(r, 0, sizeof *r);
    out->data = 0; out->len = 0; out->cap = 0; out->fail = 0;
    if (job->film < 0 || job->film >= FILM_COUNT) { r->rc = FD_E_FILM; return r->rc; }
    if (quality < 10) quality = 10;
    if (quality > 100) quality = 100;
    if (srclen > 0xFFFFFFF0u) { r->rc = SE_E_UNSUPPORTED; return r->rc; }
    fd_prepare(src0, srclen0, &src, &srclen, &owned, &prc);          /* a CR3 or a DNG becomes a plain JPEG with the metadata in it */
    if (prc) { r->rc = prc; return r->rc; }

    /* how big is the picture, to know whether a smaller size really means resizing it */
    {
        uint32_t poff = 0, plen = 0, hl = srclen < FL_HDR_BYTES ? (uint32_t)srclen : FL_HDR_BYTES;
        fl_meta meta;
        se_jdec_info ji;
        int rc = fl_find_jpeg(src, hl, (uint32_t)srclen, &poff, &plen, &meta);
        if (rc == SE_OK && (size_t)poff + plen <= srclen) {
            uint32_t jl = plen < FL_HDR_BYTES ? plen : FL_HDR_BYTES;
            rc = se_jpeg_dec_info(src + poff, jl, &ji);
            if (rc == SE_OK) { sw = ji.width; sh = ji.height; }
        }
    }
    if (sz > FD_SIZE_FULL && sz < FD_SIZE_COUNT && sw > 0) {
        fd_target(sw, sh, sz, &tw, &th);
        resize = (tw != sw || th != sh);
    }

    s.p = src; s.n = srclen;
    memset(&io, 0, sizeof io);
    io.src_size = (uint32_t)srclen;
    io.read = fd_rd; io.rctx = &s;
    io.write = fd_wr; io.wctx = out;
    io.progress = progress; io.pctx = pctx;
    io.mem_size = fl_mem_needed(sw > 6000 ? sw : 6000, 1) + fl_look_extra(sw > 6000 ? sw : 6000, &job->look);
    io.mem = (uint8_t *)malloc(io.mem_size);
    if (!io.mem) { free(owned); r->rc = SE_E_NOMEM; return r->rc; }
    f = films[job->film];

    if (resize)
        fd_develop_sized(&io, &f, &job->look, sz, quality, &res);
    else                                                 /* the whole picture, or one of the engine's own sizes */
        fl_develop(&io, &f, &job->look, sz > FD_SIZE_ENGINE ? sz - FD_SIZE_ENGINE : 1, quality, &res);

    free(io.mem);
    free(owned);
    r->rc = res.rc;
    r->src_w = res.src_w; r->src_h = res.src_h; r->out_w = res.out_w; r->out_h = res.out_h; r->mem_peak = res.mem_peak;
    if (r->rc == 0 && out->fail) r->rc = SE_E_NOMEM;
    if (r->rc != 0) { free(out->data); out->data = 0; out->len = 0; out->cap = 0; }
    return r->rc;
}

#endif
