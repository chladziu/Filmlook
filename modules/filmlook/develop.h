/* develop.h - "develop" one photo with a film look: read the JPEG that the camera made (the preview that Canon embeds in
 * every CR2, or a .JPG file), decode it at 1/1, 1/2 or 1/4 size, run the film look over it row by row and write a new
 * JPEG. Nothing here knows about the camera or the PC: the file access goes through the fl_io callbacks, so the same code
 * is tested on a PC (tools/hostdev.c) and runs in the module (filmlook.c).
 * Streaming everywhere: nothing larger than a few hundred KB is ever allocated, the frame itself is never held. */
#ifndef FL_DEVELOP_H
#define FL_DEVELOP_H

#include "engine/se_libc.h"
#include "lookbuild.h"
#include "engine/jpeg_dec.h"
#include "engine/jpeg_enc.h"

#define FL_HDR_BYTES (64 * 1024)

typedef struct {
    int (*read)(void *ctx, uint32_t off, void *dst, uint32_t n);      /* source file, absolute offsets; 0 = ok */
    void *rctx;
    uint32_t src_size;
    int (*write)(void *ctx, const uint8_t *data, size_t n);           /* destination; 0 = ok */
    void *wctx;
    uint8_t *mem;                                                      /* working memory */
    size_t   mem_size;
    void (*progress)(void *ctx, int permille);                         /* optional: how far the rows are, 0..1000 */
    void *pctx;
} fl_io;

typedef struct {
    int orient, iso;
    uint32_t en, ed, fn, fd;
    char make[24], model[32], dt[24];
} fl_meta;

typedef struct {
    int src_w, src_h, out_w, out_h;
    int rc;                 /* 0 = ok, otherwise an se_err */
    int look_radius;        /* halation radius really used */
    int scale;              /* 1 = full size, 2 = half size: what was really delivered */
    size_t mem_peak;        /* working memory really used */
} fl_result;

/* --- TIFF / EXIF header of the source (CR2 file start or the APP1 segment of a JPG) --- */
static uint32_t fl_r16(const uint8_t *p, int be) { return be ? (uint32_t)((p[0] << 8) | p[1]) : (uint32_t)((p[1] << 8) | p[0]); }
static uint32_t fl_r32(const uint8_t *p, int be)
{
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

/* value of an IFD entry: pointer to its data (inline or at the offset), or NULL when it does not fit */
static const uint8_t *fl_entry_data(const uint8_t *t, uint32_t len, const uint8_t *e, int be, uint32_t bytes)
{
    if (bytes <= 4) return e + 8;
    { uint32_t off = fl_r32(e + 8, be); if (off + bytes > len) return 0; return t + off; }
}

static void fl_copy_str(char *dst, int cap, const uint8_t *src, uint32_t n)
{
    uint32_t i = 0;
    while (i + 1 < (uint32_t)cap && i < n && src[i]) { dst[i] = (char)src[i]; i++; }
    dst[i] = 0;
}

static void fl_tiff_ifd(const uint8_t *t, uint32_t len, uint32_t ifd, int be, fl_meta *m, int depth)
{
    uint32_t n, i;
    if (ifd + 2 > len) return;
    n = fl_r16(t + ifd, be);
    if (n > 200 || ifd + 2 + 12 * n > len) return;
    for (i = 0; i < n; i++) {
        const uint8_t *e = t + ifd + 2 + 12 * i;
        uint32_t tag = fl_r16(e, be), type = fl_r16(e + 2, be), cnt = fl_r32(e + 4, be);
        const uint8_t *d;
        if (type == 2 && cnt > 0 && cnt < 64) {
            d = fl_entry_data(t, len, e, be, cnt);
            if (!d) continue;
            if (tag == 0x010f) fl_copy_str(m->make, sizeof m->make, d, cnt);
            else if (tag == 0x0110) fl_copy_str(m->model, sizeof m->model, d, cnt);
            else if (tag == 0x0132) fl_copy_str(m->dt, sizeof m->dt, d, cnt);
        } else if (tag == 0x0112 && type == 3) m->orient = (int)fl_r16(e + 8, be);
        else if (tag == 0x8827 && type == 3) m->iso = (int)fl_r16(e + 8, be);
        else if ((tag == 0x829a || tag == 0x829d) && type == 5 && cnt == 1) {
            d = fl_entry_data(t, len, e, be, 8);
            if (!d) continue;
            if (tag == 0x829a) { m->en = fl_r32(d, be); m->ed = fl_r32(d + 4, be); }
            else               { m->fn = fl_r32(d, be); m->fd = fl_r32(d + 4, be); }
        } else if (tag == 0x8769 && depth < 1) fl_tiff_ifd(t, len, fl_r32(e + 8, be), be, m, depth + 1);
    }
}

static void fl_tiff_meta(const uint8_t *t, uint32_t len, fl_meta *m)
{
    int be;
    if (len < 16) return;
    if (t[0] == 'I' && t[1] == 'I') be = 0; else if (t[0] == 'M' && t[1] == 'M') be = 1; else return;
    fl_tiff_ifd(t, len, fl_r32(t + 4, be), be, m, 0);
}

/* --- where the JPEG is: a CR2 (IFD0 holds the full size preview) or a JPEG file --- */
static int fl_find_jpeg(const uint8_t *h, uint32_t len, uint32_t size, uint32_t *off, uint32_t *bytes, fl_meta *m)
{
    memset(m, 0, sizeof *m);
    if (len > 12 && h[0] == 'I' && h[1] == 'I' && h[2] == 42 && h[8] == 'C' && h[9] == 'R') {      /* CR2 */
        uint32_t ifd = fl_r32(h + 4, 0), n, i, so = 0, sb = 0, comp = 0;
        fl_tiff_meta(h, len, m);
        if (ifd + 2 > len) return SE_E_TRUNCATED;
        n = fl_r16(h + ifd, 0);
        if (n > 100 || ifd + 2 + 12 * n > len) return SE_E_BADFILE;
        for (i = 0; i < n; i++) {
            const uint8_t *e = h + ifd + 2 + 12 * i;
            uint32_t tag = fl_r16(e, 0);
            if (tag == 0x0103) comp = fl_r32(e + 8, 0) & 0xFFFF;
            else if (tag == 0x0111) so = fl_r32(e + 8, 0);
            else if (tag == 0x0117) sb = fl_r32(e + 8, 0);
        }
        if (comp != 6 || !so || !sb || so + 2 > size) return SE_E_UNSUPPORTED;      /* SOI is checked when the JPEG is read */
        *off = so; *bytes = sb;
        return SE_OK;
    }
    if (len > 4 && h[0] == 0xFF && h[1] == 0xD8) {                                                  /* JPEG */
        uint32_t p = 2;
        while (p + 4 < len && h[p] == 0xFF) {                       /* find the Exif APP1 */
            uint32_t seg = ((uint32_t)h[p + 2] << 8) | h[p + 3];
            if (h[p + 1] == 0xE1 && seg > 14 && p + 2 + seg <= len && h[p + 4] == 'E' && h[p + 5] == 'x') {
                fl_tiff_meta(h + p + 10, seg - 8, m);
                break;
            }
            if (h[p + 1] == 0xDA) break;
            p += 2 + seg;
        }
        *off = 0; *bytes = size;
        return SE_OK;
    }
    return SE_E_BADFILE;
}

/* --- the pipeline --- */
typedef struct {
    const fl_io *io;
    uint32_t base;
    se_jpeg *enc;
    se_look_run *run;
    int ow, err;
    int rows, oh;                                       /* output rows made so far, and all of them */
} fl_ctx;

static int fl_src_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    fl_ctx *c = (fl_ctx *)ctx;
    return c->io->read(c->io->rctx, c->base + off, dst, n);
}

static int fl_enc_sink(void *ctx, const uint8_t *data, size_t len)
{
    const fl_io *io = ((fl_ctx *)ctx)->io;
    return io->write(io->wctx, data, len);
}

static int fl_look_emit(void *ctx, const uint8_t *row, int width)
{
    fl_ctx *c = (fl_ctx *)ctx;
    (void)width;
    if (se_jpeg_write_rows(c->enc, row, 1) != SE_OK) { c->err = 1; return -1; }
    return 0;
}

static int fl_look_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    fl_ctx *c = (fl_ctx *)ctx;
    if (width != c->ow) { c->err = 1; return -1; }
    if (se_look_rows(c->run, rgb, nrows, fl_look_emit, c) != SE_OK) { c->err = 1; return -1; }
    c->rows += nrows;
    if (c->io->progress) c->io->progress(c->io->pctx, c->rows >= c->oh ? 1000 : (c->rows * 1000) / c->oh);
    return 0;
}

/* memory the develop needs for a frame of src_w pixels at 1/dscale size */
static size_t fl_mem_needed(int src_w, int dscale)
{
    size_t ow = (size_t)((src_w + dscale - 1) / dscale);
    return FL_HDR_BYTES + ow * 3 * 8 + 8 * 1024 + ow * 16 * 6 + 16 * 1024 + SE_LOOK_BUDGET + 12 * 1024 + ow * 4 + 4096;
}

/* dscale: 1, 2 or 4. quality: JPEG quality of the result. */
static void fl_develop(const fl_io *io, const film_t *f, const look_t *L, int dscale, int quality, fl_result *res)
{
    se_arena ar, lookar;
    se_look lk;
    se_jdec_info ji;
    se_jpeg_exif ex;
    fl_meta meta;
    fl_ctx c;
    uint8_t *hdr, *strip;
    uint32_t hlen, poff = 0, plen = 0;
    int ow, oh;
    size_t lookneed;
    se_err rc;

    memset(res, 0, sizeof *res);
    se_arena_init(&ar, io->mem, io->mem_size);
    hdr = SE_NEW(&ar, uint8_t, FL_HDR_BYTES);
    if (!hdr) { res->rc = SE_E_NOMEM; return; }
    hlen = io->src_size < FL_HDR_BYTES ? io->src_size : FL_HDR_BYTES;
    if (io->read(io->rctx, 0, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = fl_find_jpeg(hdr, hlen, io->src_size, &poff, &plen, &meta);
    if (rc != SE_OK) { res->rc = rc; return; }
    /* the header buffer is free again (the metadata is copied): load the first bytes of the JPEG itself into it */
    if (plen > io->src_size || poff > io->src_size - plen) { res->rc = SE_E_TRUNCATED; return; }
    hlen = plen < FL_HDR_BYTES ? plen : FL_HDR_BYTES;
    if (io->read(io->rctx, poff, hdr, hlen) != 0) { res->rc = SE_E_IO; return; }
    rc = se_jpeg_dec_info(hdr, hlen, &ji);
    if (rc != SE_OK) { res->rc = rc; return; }
    if (dscale == 2 && ji.width < 3200) dscale = 1;          /* a small JPG (S / M size) is not made even smaller */
    se_jpeg_scaled_size(ji.width, ji.height, dscale, &ow, &oh);
    res->src_w = ji.width; res->src_h = ji.height; res->out_w = ow; res->out_h = oh; res->scale = dscale;

    /* the look: its tables and the halation delay line live in their own arena */
    {
        size_t tabs = 3 * 1536 * sizeof(int16_t) + 64;
        size_t budget = SE_LOOK_BUDGET + (size_t)ow * 4 + 4096;
        uint8_t *mem = (uint8_t *)se_arena_alloc(&ar, tabs + budget, 8);
        if (!mem) { res->rc = SE_E_NOMEM; return; }
        se_arena_init(&lookar, mem, tabs + budget);
        if (fl_build_look(&lk, &lookar, f, L, dscale, oh) != 0) { res->rc = SE_E_NOMEM; return; }
        lookneed = se_look_fit(&lk, ow, budget);
        (void)lookneed;
        res->look_radius = lk.hal_amount > 0 ? lk.hal_radius : 0;
        rc = se_look_begin(&c.run, &lk, ow, &lookar);
        if (rc != SE_OK) { res->rc = rc; return; }
    }

    c.io = io; c.base = poff; c.ow = ow; c.err = 0; c.rows = 0; c.oh = oh > 0 ? oh : 1;
    c.enc = SE_NEW(&ar, se_jpeg, 1);
    strip = SE_NEW(&ar, uint8_t, (size_t)8 * ow * 3);
    if (!c.enc || !strip) { res->rc = SE_E_NOMEM; return; }

    memset(&ex, 0, sizeof ex);
    ex.make = meta.make[0] ? meta.make : "Canon";
    ex.model = meta.model[0] ? meta.model : "Canon EOS 5D Mark II";
    ex.software = f->full;
    ex.datetime = meta.dt;
    ex.exp_num = meta.en; ex.exp_den = meta.ed; ex.fn_num = meta.fn; ex.fn_den = meta.fd;
    ex.iso = meta.iso; ex.orientation = meta.orient;
    rc = se_jpeg_begin(c.enc, ow, oh, quality, strip, &ex, fl_enc_sink, &c);
    if (rc != SE_OK) { res->rc = rc; return; }

    rc = se_jpeg_decode_stream_scaled(hdr, hlen, plen, fl_src_read, &c, dscale, &ar, fl_look_rows, &c, &ji);
    if (rc == SE_OK && !c.err) rc = se_look_end(c.run, fl_look_emit, &c);
    if (rc == SE_OK && !c.err) rc = se_jpeg_end(c.enc);
    if (rc == SE_OK && c.err) rc = SE_E_IO;
    res->rc = rc;
    res->mem_peak = ar.peak;
}

#endif
