/* PC test of the JPG engine: hostdev <source.CR2|.JPG> <out.jpg> <film name or index> [scale] [key=value ...]
 * keys: strength grain contrast fade warm shadow high sat vig hal quality */
#define SE_HOSTED 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../develop.h"

static FILE *g_in, *g_out;
static int rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{ (void)ctx; if (fseek(g_in, (long)off, SEEK_SET)) return 1; return fread(dst, 1, n, g_in) == n ? 0 : 1; }
static int wr(void *ctx, const uint8_t *d, size_t n)
{ (void)ctx; return fwrite(d, 1, n, g_out) == n ? 0 : 1; }

int main(int argc, char **argv)
{
    look_t L = { 100, 100, 0, 0, 0, 0, 0, 0, 0, 0 };
    int i, fi = -1, scale = 2, quality = 90;
    fl_io io; fl_result res; clock_t t0;
    if (argc < 4) { fprintf(stderr, "usage\n"); return 2; }
    for (i = 0; i < FILM_COUNT; i++) if (!strcmp(films[i].full, argv[3])) fi = i;
    if (fi < 0) fi = atoi(argv[3]);
    if (fi < 0 || fi >= FILM_COUNT) { fprintf(stderr, "no such film\n"); return 2; }
    if (argc > 4) scale = atoi(argv[4]);
    static fv_t ov[ATTR_COUNT + 8];
    film_t fcopy = films[fi];
    int nov = 0;
    for (i = 0; i < films[fi].nv; i++) ov[nov++] = films[fi].v[i];
    for (i = 5; i < argc; i++) {
        char *eq = strchr(argv[i], '='); int v;
        if (!eq) continue;
        *eq = 0; v = atoi(eq + 1);
        if (!strcmp(argv[i], "set")) {                       /* set=Attribute:value, replaces a value of the film */
            char *co = strchr(eq + 1, ':');
            if (co) {
                int id, k;
                *co = 0; id = xa_id(eq + 1); v = atoi(co + 1);
                if (id < 0) { fprintf(stderr, "unknown attribute %s\n", eq + 1); return 2; }
                for (k = 0; k < nov && ov[k].id != id; k++) {}
                if (k == nov) nov++;
                ov[k].id = (unsigned char)id; ov[k].v = (short)v;
            }
            continue;
        }
        if (!strcmp(argv[i], "strength")) L.strength = v; else if (!strcmp(argv[i], "grain")) L.grain = v;
        else if (!strcmp(argv[i], "contrast")) L.contrast = v; else if (!strcmp(argv[i], "fade")) L.fade = v;
        else if (!strcmp(argv[i], "warm")) L.warm = v; else if (!strcmp(argv[i], "shadow")) L.shadow = v;
        else if (!strcmp(argv[i], "high")) L.high = v; else if (!strcmp(argv[i], "sat")) L.sat = v;
        else if (!strcmp(argv[i], "vig")) L.vig = v; else if (!strcmp(argv[i], "hal")) L.hal = v;
        else if (!strcmp(argv[i], "quality")) quality = v;
    }
    g_in = fopen(argv[1], "rb"); g_out = fopen(argv[2], "wb");
    if (!g_in || !g_out) { perror("open"); return 1; }
    memset(&io, 0, sizeof io);
    fseek(g_in, 0, SEEK_END); io.src_size = (uint32_t)ftell(g_in);
    io.read = rd; io.rctx = 0; io.write = wr; io.wctx = 0;
    io.mem_size = fl_mem_needed(6000, scale);
    io.mem = (uint8_t *)malloc(io.mem_size);
    t0 = clock();
    fcopy.v = ov; fcopy.nv = (unsigned char)nov;
    fl_develop(&io, &fcopy, &L, scale, quality, &res);
    fclose(g_out);
    fprintf(stderr, "%s: rc=%d %dx%d -> %dx%d radius %d  %.2fs  mem %zu KB\n", films[fi].full, res.rc, res.src_w, res.src_h,
            res.out_w, res.out_h, res.look_radius, (double)(clock() - t0) / CLOCKS_PER_SEC, io.mem_size >> 10);
    fprintf(stderr, "arena peak %zu KB of %zu KB\n", res.mem_peak >> 10, io.mem_size >> 10);
    return res.rc != 0;
}
