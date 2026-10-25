/* PC test of the viewer: viewtest <file.jpg> <out_prefix> [lev] [cx] [cy]
 *   - writes <out_prefix>_<lev>.ppm: the very frame that the camera would put on its screen (palette picture, strips, badge)
 *   - tests the list of the pictures (order by number, the full list drops the oldest)
 *   - prints how long the decoding and the palette picture took on this PC (the camera is much slower: only the proportions count)
 * It runs the same viewer.h / panel.h / serif.h / engine code as the camera. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#define VERSION "v0.11"
#include "../films.h"
#include "../state.h"
#include "../panel.h"
#include "../viewer.h"

static uint8_t *g_file; static uint32_t g_size;
static int rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{ (void)ctx; if (off + n > g_size) return 1; memcpy(dst, g_file + off, n); return 0; }

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void test_list(void)
{
    int i;
    vw_n = 0;
    CHECK(vw_list_add("IMG_0010.JPG", 0) == 1);
    CHECK(vw_list_add("IMG_0002.JPG", 0) == 1);
    CHECK(vw_list_add("_MG_0005.JPG", 0) == 1);
    CHECK(vw_list_add("WRITING.TMP", 0) == 0 && vw_list_add("IMG_0003.CR2", 0) == 0 && vw_list_add(".IMG_1.JPG", 0) == 0);
    CHECK(vw_list_add("img_0100.jpg", 0) == 1);                    /* lower case is a JPG too */
    vw_list_sort();
    CHECK(vw_n == 4 && !strcmp(vw_list[0].name, "IMG_0002.JPG") && !strcmp(vw_list[1].name, "_MG_0005.JPG") && !strcmp(vw_list[2].name, "IMG_0010.JPG") && !strcmp(vw_list[3].name, "img_0100.jpg"));
    vw_n = 0;
    for (i = 1; i <= VW_MAXPH + 5; i++) { char nm[16]; snprintf(nm, sizeof nm, "IMG_%04d.JPG", i); vw_list_add(nm, 0); }
    vw_list_sort();
    CHECK(vw_n == VW_MAXPH && vw_key(vw_list[0].name) == 6 && vw_key(vw_list[VW_MAXPH - 1].name) == VW_MAXPH + 5);      /* the oldest five went */
    vw_n = 0;
}

static void test_layout(void)
{
    vw_pic p;
    vw_lay L;
    int cx = 100, cy = 100;
    vw_pic_set(&p, 5616, 3744);
    CHECK(p.dfit == 8 && p.nlev == 4);
    vw_layout(&p, 0, &cx, &cy, &L);
    CHECK(L.d == 8 && L.iw == 702 && L.ih == 468 && L.dw == 720 && L.dh == 427 && L.dx == 0 && L.dy == 26 && cx == 2808 && cy == 1872);
    CHECK(vw_has_bars(&L) && vw_percent(&p, &L, 0) == 11);
    cx = 0; cy = 0;
    vw_layout(&p, 3, &cx, &cy, &L);                                  /* 100 %: the corner, clamped inside the picture */
    CHECK(L.d == 1 && L.sx == 0 && L.sy == 0 && L.sw == 640 && L.sh == 480 && L.dw == 720 && L.dh == 480 && !vw_has_bars(&L) && cx == 320 && cy == 240);
    cx = 99999; cy = 99999;
    vw_layout(&p, 1, &cx, &cy, &L);                                  /* 25 %: the other corner */
    CHECK(L.d == 4 && L.sx + L.sw == L.iw && L.sy + L.sh == L.ih);
    vw_pic_set(&p, 2808, 1872);
    CHECK(p.dfit == 4 && p.nlev == 3);
    vw_pic_set(&p, 1200, 800);
    CHECK(p.dfit == 1 && p.nlev == 1);
    vw_pic_set(&p, 3744, 5616);                                       /* portrait */
    cx = 0; cy = 0;
    vw_layout(&p, 0, &cx, &cy, &L);
    CHECK(L.dh == 480 && L.dw <= 720 && L.dw % 2 == 0 && !vw_has_bars(&L));
}

int main(int argc, char **argv)
{
    FILE *f;
    int lev, cx, cy, i, x, y;
    vw_pic p;
    vw_lay L;
    vw_tile t;
    vw_src src;
    se_arena ar;
    static uint8_t arena_mem[512 * 1024];
    static uint8_t frame[VW_W * VW_H];
    se_jdec_info ji;
    se_err rc;
    int tx, ty, tw, th;
    double t0, t1, t2;
    char name[256];

    test_list();
    test_layout();
    if (argc < 3) { printf("%s\n", fails ? "viewer test FAILED" : "viewer logic test passed (no picture given)"); return fails != 0; }

    memo_init(); sets_init();
    pal_build();
    ui_fb = frame; ui_pitch = VW_W;
    f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); g_size = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    g_file = malloc(g_size);
    if (fread(g_file, 1, g_size, f) != g_size) return 2;
    fclose(f);
    lev = argc > 3 ? atoi(argv[3]) : 0;
    if (se_jpeg_dec_info(g_file, g_size < 16384 ? g_size : 16384, &ji) != SE_OK) { printf("not a JPEG we can read\n"); return 2; }
    vw_pic_set(&p, ji.width, ji.height);
    cx = argc > 4 ? atoi(argv[4]) : p.W / 2;
    cy = argc > 5 ? atoi(argv[5]) : p.H / 2;
    if (lev >= p.nlev) lev = p.nlev - 1;
    vw_layout(&p, lev, &cx, &cy, &L);
    printf("picture %dx%d  fit 1/%d  levels %d  -> level %d: 1/%d, part %d,%d %dx%d of %dx%d, on the screen %d,%d %dx%d\n",
           p.W, p.H, p.dfit, p.nlev, lev, L.d, L.sx, L.sy, L.sw, L.sh, L.iw, L.ih, L.dx, L.dy, L.dw, L.dh);

    t.cap = 3u << 20; t.rgb = malloc(t.cap); t.ok = 0;
    vw_hist = malloc(32768 * sizeof(uint32_t)); vw_inv = malloc(32768);
    CHECK(vw_tile_plan(&L, t.cap, &tx, &ty, &tw, &th));
    printf("tile %d,%d %dx%d (%d KB of %d KB)\n", tx, ty, tw, th, (int)((size_t)tw * th * 3 >> 10), (int)(t.cap >> 10));
    se_arena_init(&ar, arena_mem, sizeof arena_mem);
    src.read = rd; src.rctx = 0; src.size = g_size; src.hdr = g_file; src.hdrlen = g_size < 16384 ? g_size : 16384; src.stop = 0; src.sctx = 0;
    t0 = now_ms();
    rc = vw_decode_tile(&src, L.d, &t, tx, ty, tw, th, &ar);
    t1 = now_ms();
    CHECK(rc == SE_OK && t.ok && vw_tile_has(&t, &L));
    if (rc != SE_OK) { printf("decoding failed: %d\n", rc); return 1; }
    printf("arena used: %d KB\n", (int)(ar.used >> 10));
    snprintf(name, sizeof name, "IMG_1234.JPG");
    vw_compose(frame, &t, &p, &L, lev, name, 6, 48);
    t2 = now_ms();
    printf("decode %.0f ms, palette picture %.0f ms, %d palette entries\n", t1 - t0, t2 - t1, vw_nbx);
    CHECK(vw_nbx > 0 && vw_nbx <= VW_COLS);
    for (i = 0; i < VW_W * VW_H; i++) CHECK(frame[i] != 0 && (frame[i] <= vw_nbx || frame[i] >= PAL_BASE));
    /* the frame as a PPM: entries 1..vw_nbx are the picture's colours, PAL_BASE.. the panel's */
    snprintf(name, sizeof name, "%s_%d.ppm", argv[2], lev);
    f = fopen(name, "wb");
    fprintf(f, "P6\n%d %d\n255\n", VW_W, VW_H);
    for (y = 0; y < VW_H; y++) for (x = 0; x < VW_W; x++) {
        int e = frame[y * VW_W + x];
        const unsigned char *c = e >= PAL_BASE ? pal_rgb[e - PAL_BASE] : vw_prgb[e - 1];
        fwrite(c, 1, 3, f);
    }
    fclose(f);
    printf("%s\n%s\n", name, fails ? "viewer test FAILED" : "viewer test passed");
    return fails != 0;
}
