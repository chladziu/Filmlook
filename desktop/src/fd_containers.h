/* fd_containers.h - more kinds of photo files for FILM LOOK DESKTOP: CR3 (Canon EOS R, an ISO box file) and DNG (a TIFF file).
 *
 * Like for a CR2, the program does not decode the raw data of the sensor: it takes the full size JPEG that is stored inside the
 * file (the camera or Lightroom made it from the raw picture) and develops that. This file
 *   1. finds that JPEG (fd_cr3_find, fd_dng_find),
 *   2. reads the camera's metadata (orientation, camera name, date, exposure) with the engine's own reader (fl_tiff_meta), and
 *   3. makes a plain JPEG of it that carries the metadata in an Exif block, which is what the engine reads from a JPG
 *      (fd_prepare). So nothing in the engine itself has to change.
 *
 * CR3: ftyp | moov { uuid(Canon: CMT1 = Exif IFD0, CMT2 = Exif IFD) , trak x4 , ... } | uuid(PRVW) | mdat.
 *      The full size JPEG is the sample of the 'vide' track whose sample description (stsd) has a 'JPEG' box inside
 *      (offset: stco / co64, size: stsz). When there is none, the 1620 x 1080 preview of the PRVW box is used.
 * DNG: a TIFF. IFD0 and its SubIFDs hold the thumbnail, the previews and the raw data. The biggest JPEG-compressed image with
 *      the photometric interpretation RGB or YCbCr is the preview (the raw data is JPEG-compressed too, but "lossless", with
 *      the interpretation CFA or LinearRaw: that one is never taken).
 * Own code of the film look project. */
#ifndef FD_CONTAINERS_H
#define FD_CONTAINERS_H

#define FC4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t fc_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* the next box in [*pos, end): its type and the range of its content. 0 = there is none (or it is damaged) */
static int fc_next(const uint8_t *f, size_t end, size_t *pos, uint32_t *type, size_t *cs, size_t *ce)
{
    size_t p = *pos, size, hl = 8;
    if (p + 8 > end) return 0;
    size = fc_be32(f + p);
    *type = fc_be32(f + p + 4);
    if (size == 1) {                                                   /* 64 bit size */
        if (p + 16 > end || fc_be32(f + p + 8) != 0) return 0;
        size = fc_be32(f + p + 12); hl = 16;
    } else if (size == 0) size = end - p;                              /* to the end */
    if (size < hl || size > end - p) return 0;
    *cs = p + hl; *ce = p + size; *pos = p + size;
    return 1;
}

/* the first child box of the given type inside [s, e) */
static int fc_child(const uint8_t *f, size_t s, size_t e, uint32_t type, size_t *cs, size_t *ce)
{
    size_t pos = s, a, b;
    uint32_t t;
    while (fc_next(f, e, &pos, &t, &a, &b)) if (t == type) { *cs = a; *ce = b; return 1; }
    return 0;
}

static int fc_is_jpeg(const uint8_t *f, size_t n, size_t off, size_t len)
{
    return len > 4 && off < n && len <= n - off && f[off] == 0xFF && f[off + 1] == 0xD8 && f[off + 2] == 0xFF;
}

static int fc_same(const uint8_t *a, const uint8_t *b, size_t n) { while (n--) if (*a++ != *b++) return 0; return 1; }

static const uint8_t fc_uuid_canon[16] = { 0x85, 0xc0, 0xb6, 0x87, 0x82, 0x0f, 0x11, 0xe0, 0x81, 0x11, 0xf4, 0xce, 0x46, 0x2b, 0x6a, 0x48 };
static const uint8_t fc_uuid_prvw[16]  = { 0xea, 0xf4, 0x2b, 0x5e, 0x1c, 0x98, 0x4b, 0x88, 0xb9, 0xfb, 0xb7, 0xdc, 0x40, 0x6e, 0x4d, 0x16 };

/* the 'uuid' box of the PRVW preview: 16 bytes uuid, 8 bytes "0 1", the 'PRVW' box (size, name, 0, 1, width, height, 1, JPEG size),
 * then the JPEG at 0x30 */
static int fc_prvw(const uint8_t *f, size_t n, size_t s, size_t e, size_t *off, size_t *len)
{
    size_t l;
    if (e - s < 0x30 || !fc_same(f + s, fc_uuid_prvw, 16)) return 0;
    l = fc_be32(f + s + 0x2c);
    if (s + 0x30 + l > e || !fc_is_jpeg(f, n, s + 0x30, l)) return 0;
    *off = s + 0x30; *len = l;
    return 1;
}

/* the JPEG of one 'trak': 1 = found. The track is the one with a 'JPEG' box in its sample description */
static int fc_trak_jpeg(const uint8_t *f, size_t n, size_t s, size_t e, size_t *off, size_t *len)
{
    size_t a, b, ms, me, is, ie, ss, se, sd_s, sd_e, k, count, size = 0, o = 0;
    uint32_t t;
    if (!fc_child(f, s, e, FC4('m', 'd', 'i', 'a'), &ms, &me)) return 0;
    if (!fc_child(f, ms, me, FC4('m', 'i', 'n', 'f'), &is, &ie)) return 0;
    if (!fc_child(f, is, ie, FC4('s', 't', 'b', 'l'), &ss, &se)) return 0;
    if (!fc_child(f, ss, se, FC4('s', 't', 's', 'd'), &sd_s, &sd_e)) return 0;
    for (k = sd_s; k + 4 <= sd_e && k < sd_s + 1024; k++)                 /* a 'JPEG' box inside the sample description */
        if (f[k] == 'J' && f[k + 1] == 'P' && f[k + 2] == 'E' && f[k + 3] == 'G') break;
    if (k + 4 > sd_e || k >= sd_s + 1024) return 0;
    if (!fc_child(f, ss, se, FC4('s', 't', 's', 'z'), &a, &b) || b - a < 12) return 0;         /* version/flags, sample size, count */
    count = fc_be32(f + a + 8);
    size = fc_be32(f + a + 4);
    if (!count) return 0;
    if (!size) { if (b - a < 16) return 0; size = fc_be32(f + a + 12); }                       /* a table of sizes: the first one */
    t = 0;
    if (fc_child(f, ss, se, FC4('s', 't', 'c', 'o'), &a, &b) && b - a >= 12 && fc_be32(f + a + 4) >= 1) { o = fc_be32(f + a + 8); t = 1; }
    else if (fc_child(f, ss, se, FC4('c', 'o', '6', '4'), &a, &b) && b - a >= 16 && fc_be32(f + a + 4) >= 1) {
        if (fc_be32(f + a + 8) != 0) return 0;
        o = fc_be32(f + a + 12); t = 1;
    }
    if (!t || !fc_is_jpeg(f, n, o, size)) return 0;
    *off = o; *len = size;
    return 1;
}

/* a CR3: the full size JPEG (or the 1620 x 1080 preview) and the metadata of the camera */
static int fd_cr3_find(const uint8_t *f, size_t n, size_t *poff, size_t *plen, fl_meta *m)
{
    size_t pos = 0, s, e, moov_s = 0, moov_e = 0, po = 0, pl = 0, jo = 0, jl = 0;
    uint32_t t;
    memset(m, 0, sizeof *m);
    if (n < 32 || fc_be32(f + 4) != FC4('f', 't', 'y', 'p')) return SE_E_BADFILE;
    while (fc_next(f, n, &pos, &t, &s, &e)) {
        if (t == FC4('m', 'o', 'o', 'v') && !moov_s) { moov_s = s; moov_e = e; }
        else if (t == FC4('u', 'u', 'i', 'd') && !pl) fc_prvw(f, n, s, e, &po, &pl);
    }
    if (!moov_s) return SE_E_BADFILE;
    pos = moov_s;
    while (fc_next(f, moov_e, &pos, &t, &s, &e)) {
        if (t == FC4('u', 'u', 'i', 'd')) {
            if (e - s > 16 && fc_same(f + s, fc_uuid_canon, 16)) {
                size_t c = s + 16, a, b;
                uint32_t ct;
                while (fc_next(f, e, &c, &ct, &a, &b)) {
                    if (ct == FC4('C', 'M', 'T', '1') || ct == FC4('C', 'M', 'T', '2')) fl_tiff_meta(f + a, (uint32_t)(b - a), m);
                }
            } else if (!pl) fc_prvw(f, n, s, e, &po, &pl);
        } else if (t == FC4('t', 'r', 'a', 'k') && !jl) fc_trak_jpeg(f, n, s, e, &jo, &jl);
    }
    if (jl) { *poff = jo; *plen = jl; return SE_OK; }
    if (pl) { *poff = po; *plen = pl; return SE_OK; }
    return SE_E_UNSUPPORTED;
}

/* ---------------------------------------------------------------- DNG */
typedef struct { const uint8_t *f; size_t n; int be; } fc_tiff;
static uint32_t ft16(const fc_tiff *t, size_t o) { return fl_r16(t->f + o, t->be); }
static uint32_t ft32(const fc_tiff *t, size_t o) { return fl_r32(t->f + o, t->be); }

/* the first number of an entry (SHORT or LONG) and where its values are */
static uint32_t ft_num(const fc_tiff *t, size_t e) { return ft16(t, e + 2) == 3 ? ft16(t, e + 8) : ft32(t, e + 8); }
static size_t ft_vals(const fc_tiff *t, size_t e)       /* offset of the values; 0 when out of the file */
{
    uint32_t type = ft16(t, e + 2), cnt = ft32(t, e + 4), sz = type == 3 ? 2u : (type == 4 || type == 13 ? 4u : 1u);
    size_t bytes = (size_t)cnt * sz;
    if (cnt > 0x100000u) return 0;
    if (bytes <= 4) return e + 8;
    { size_t o = ft32(t, e + 8); return (o + bytes <= t->n) ? o : 0; }
}

#define FC_MAXIFD 40
static int fd_dng_find(const uint8_t *f, size_t n, size_t *poff, size_t *plen, fl_meta *m)
{
    fc_tiff t;
    size_t queue[FC_MAXIFD], qn = 0, qi, best_o = 0, best_l = 0;
    uint64_t best_score = 0;
    int best_ori = 0;
    memset(m, 0, sizeof *m);
    if (n < 16) return SE_E_BADFILE;
    t.f = f; t.n = n;
    if (f[0] == 'I' && f[1] == 'I' && f[2] == 42 && f[3] == 0) t.be = 0;
    else if (f[0] == 'M' && f[1] == 'M' && f[2] == 0 && f[3] == 42) t.be = 1;
    else return SE_E_BADFILE;
    fl_tiff_meta(f, (uint32_t)n, m);                                     /* camera, date, orientation, exposure */
    queue[qn++] = ft32(&t, 4);
    for (qi = 0; qi < qn; qi++) {
        size_t ifd = queue[qi], i, cnt, e, next;
        uint32_t w = 0, h = 0, comp = 0, photo = 0, ori = 0, nstrips = 0, so = 0, sc = 0, jif = 0, jil = 0;
        if (ifd == 0 || ifd + 2 > n) continue;
        cnt = ft16(&t, ifd);
        if (cnt == 0 || cnt > 300 || ifd + 2 + 12 * cnt + 4 > n) continue;
        for (i = 0; i < cnt; i++) {
            uint32_t tag, ty;
            e = ifd + 2 + 12 * i;
            tag = ft16(&t, e); ty = ft16(&t, e + 2);
            if (ty != 3 && ty != 4 && ty != 13) continue;
            switch (tag) {
            case 0x100: w = ft_num(&t, e); break;
            case 0x101: h = ft_num(&t, e); break;
            case 0x103: comp = ft_num(&t, e); break;
            case 0x106: photo = ft_num(&t, e); break;
            case 0x112: ori = ft_num(&t, e); break;
            case 0x111: nstrips = ft32(&t, e + 4); so = ft_num(&t, e); break;
            case 0x117: sc = ft_num(&t, e); break;
            case 0x201: jif = ft_num(&t, e); break;
            case 0x202: jil = ft_num(&t, e); break;
            case 0x14A: {                                                   /* SubIFDs */
                uint32_t k, c = ft32(&t, e + 4);
                size_t v = ft_vals(&t, e);
                if (!v) break;
                for (k = 0; k < c && k < 16 && qn < FC_MAXIFD; k++) queue[qn++] = ft32(&t, v + 4 * (size_t)k);
                break; }
            default: break;
            }
        }
        {
            size_t co = 0, cl = 0;
            if ((comp == 7 || comp == 6) && (photo == 6 || photo == 2) && nstrips == 1 && so && sc) { co = so; cl = sc; }
            else if (jif && jil) { co = jif; cl = jil; }
            if (cl && fc_is_jpeg(f, n, co, cl)) {
                uint64_t score = (uint64_t)(w ? w : 1) * (h ? h : 1);
                if (score > best_score) { best_score = score; best_o = co; best_l = cl; best_ori = (int)ori; }
            }
        }
        next = ifd + 2 + 12 * cnt;                                          /* the next IFD of the chain (IFD1 ...) */
        if (qi == 0 && qn < FC_MAXIFD && next + 4 <= n && ft32(&t, next)) queue[qn++] = ft32(&t, next);
    }
    if (!best_l) return SE_E_UNSUPPORTED;
    if (best_ori >= 1 && best_ori <= 8) m->orient = best_ori;               /* the preview says how it is turned itself */
    *poff = best_o; *plen = best_l;
    return SE_OK;
}

/* ---------------------------------------------------------------- a plain JPEG with the metadata in it */
typedef struct { uint8_t b[640]; size_t n; } fc_out;
static void fo_u8(fc_out *o, unsigned v) { if (o->n < sizeof o->b) o->b[o->n++] = (uint8_t)v; }
static void fo_u16(fc_out *o, unsigned v) { fo_u8(o, v & 255); fo_u8(o, v >> 8); }           /* the Exif block is little endian */
static void fo_u32(fc_out *o, uint32_t v) { fo_u16(o, v & 0xFFFF); fo_u16(o, (unsigned)(v >> 16)); }
static void fo_entry(fc_out *o, unsigned tag, unsigned type, uint32_t cnt, uint32_t val) { fo_u16(o, tag); fo_u16(o, type); fo_u32(o, cnt); fo_u32(o, val); }

/* the APP1 segment (FF E1 ...) for the fields the engine reads; returns its length */
static size_t fd_exif_block(uint8_t *dst, const fl_meta *m)
{
    fc_out o;
    size_t ifd0_at, exif_at, data_at, n0 = 0, n1 = 0, i, hdr;
    size_t lm = 0, lo = 0, ld = 0;
    int has_exif = (m->en && m->ed) || (m->fn && m->fd) || m->iso > 0;
    o.n = 0;
    for (lm = 0; m->make[lm]; lm++) ;
    for (lo = 0; m->model[lo]; lo++) ;
    for (ld = 0; m->dt[ld]; ld++) ;
    fo_u8(&o, 0xFF); fo_u8(&o, 0xE1); fo_u16(&o, 0);                                   /* length: filled in below */
    fo_u8(&o, 'E'); fo_u8(&o, 'x'); fo_u8(&o, 'i'); fo_u8(&o, 'f'); fo_u8(&o, 0); fo_u8(&o, 0);
    hdr = o.n;                                                                         /* the TIFF header starts here */
    fo_u8(&o, 'I'); fo_u8(&o, 'I'); fo_u16(&o, 42); fo_u32(&o, 8);
    n0 = (lm ? 1 : 0) + (lo ? 1 : 0) + (m->orient > 0 ? 1 : 0) + (ld ? 1 : 0) + (has_exif ? 1 : 0);
    n1 = ((m->en && m->ed) ? 1 : 0) + ((m->fn && m->fd) ? 1 : 0) + (m->iso > 0 ? 1 : 0);
    ifd0_at = o.n;
    exif_at = ifd0_at + 2 + 12 * n0 + 4;
    data_at = exif_at + (has_exif ? 2 + 12 * n1 + 4 : 0);
    o.n = ifd0_at;
    fo_u16(&o, (unsigned)n0);
    {
        uint32_t dp = (uint32_t)(data_at - hdr);                                       /* where the next long value goes (TIFF offset) */
        if (lm)  { fo_entry(&o, 0x010f, 2, (uint32_t)lm + 1, dp); dp += (uint32_t)lm + 1; }
        if (lo)  { fo_entry(&o, 0x0110, 2, (uint32_t)lo + 1, dp); dp += (uint32_t)lo + 1; }
        if (m->orient > 0) fo_entry(&o, 0x0112, 3, 1, (uint32_t)m->orient);
        if (ld)  { fo_entry(&o, 0x0132, 2, (uint32_t)ld + 1, dp); dp += (uint32_t)ld + 1; }
        if (has_exif) fo_entry(&o, 0x8769, 4, 1, (uint32_t)(exif_at - hdr));
        fo_u32(&o, 0);
        if (has_exif) {
            fo_u16(&o, (unsigned)n1);
            if (m->en && m->ed) { fo_entry(&o, 0x829a, 5, 1, dp); dp += 8; }
            if (m->fn && m->fd) { fo_entry(&o, 0x829d, 5, 1, dp); dp += 8; }
            if (m->iso > 0) fo_entry(&o, 0x8827, 3, 1, (uint32_t)m->iso);
            fo_u32(&o, 0);
        }
    }
    for (i = 0; i < lm; i++) fo_u8(&o, (uint8_t)m->make[i]);
    if (lm) fo_u8(&o, 0);
    for (i = 0; i < lo; i++) fo_u8(&o, (uint8_t)m->model[i]);
    if (lo) fo_u8(&o, 0);
    for (i = 0; i < ld; i++) fo_u8(&o, (uint8_t)m->dt[i]);
    if (ld) fo_u8(&o, 0);
    if (has_exif) {
        if (m->en && m->ed) { fo_u32(&o, m->en); fo_u32(&o, m->ed); }
        if (m->fn && m->fd) { fo_u32(&o, m->fn); fo_u32(&o, m->fd); }
    }
    if (o.n > 600) return 0;                                                           /* cannot happen: strings are short */
    /* the length field of the APP1 segment (big endian) */
    o.b[2] = (uint8_t)(((o.n - 2) >> 8) & 255); o.b[3] = (uint8_t)((o.n - 2) & 255);
    memcpy(dst, o.b, o.n);
    return o.n;
}

/* The source the engine is given: a camera JPG or a CR2 stay as they are (*use = src). A CR3 or a DNG is turned into a plain JPEG
 * (SOI + Exif block + the rest of the preview) in a new buffer that the caller frees (*owned); *rc is an se_err when there is no
 * usable preview. */
static void fd_prepare(const uint8_t *src, size_t n, const uint8_t **use, size_t *usen, uint8_t **owned, int *rc)
{
    size_t po = 0, pl = 0;
    fl_meta m;
    int r = 0, kind = 0;
    *use = src; *usen = n; *owned = 0; *rc = 0;
    if (n < 16) return;
    if (src[4] == 'f' && src[5] == 't' && src[6] == 'y' && src[7] == 'p') { kind = 1; r = fd_cr3_find(src, n, &po, &pl, &m); }
    else if ((src[0] == 'I' && src[1] == 'I' && src[2] == 42 && src[3] == 0 && !(src[8] == 'C' && src[9] == 'R')) ||
             (src[0] == 'M' && src[1] == 'M' && src[2] == 0 && src[3] == 42)) { kind = 2; r = fd_dng_find(src, n, &po, &pl, &m); }
    if (!kind) return;                                                               /* not one of these: a JPG or a CR2 */
    if (r != SE_OK) { *rc = r; return; }
    {
        uint8_t blk[640], *buf;
        size_t bl = fd_exif_block(blk, &m);
        buf = (uint8_t *)malloc(2 + bl + pl);
        if (!buf) { *rc = SE_E_NOMEM; return; }
        buf[0] = 0xFF; buf[1] = 0xD8;
        memcpy(buf + 2, blk, bl);
        memcpy(buf + 2 + bl, src + po + 2, pl - 2);                                  /* the rest of the preview after its SOI */
        *use = buf; *usen = 2 + bl + pl - 2; *owned = buf;
    }
}

#endif
