/* PC test of the JPEG decoder: dectest <file.jpg> [old_decoder_too]
 * 1. the windows: every window of every scale (1, 2, 4, 8) must be exactly the same crop of the whole picture at that scale
 * 2. the reduced scales: 1/2, 1/4, 1/8 are compared with a box filter of the full picture (the mean difference must be small)
 * 3. the early end and the stop function: a window at the top reads less of the file than one at the bottom; "stop" ends with SE_E_ABORT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../engine/se.h"
#include "../engine/jpeg_dec.h"

static uint8_t *g_file; static uint32_t g_size; static uint32_t g_read_max;
static int rd(void *ctx, uint32_t off, void *dst, uint32_t n)
{ (void)ctx; if (off + n > g_size) return 1; memcpy(dst, g_file + off, n); if (off + n > g_read_max) g_read_max = off + n; return 0; }

typedef struct { uint8_t *px; int w, h, got; int ow; } img_t;
static int sink(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    img_t *im = (img_t *)ctx;
    if (width != im->ow) return 1;
    if (im->got + nrows > im->h) return 1;
    memcpy(im->px + (size_t)im->got * width * 3, rgb, (size_t)nrows * width * 3);
    im->got += nrows;
    return 0;
}
static int stop_after(void *ctx) { int *n = (int *)ctx; return --(*n) <= 0; }

static uint8_t *arena_mem; static size_t arena_sz = 8u << 20;

static int decode(int scale, int x0, int y0, int w, int h, img_t *im, int *info_w, int *info_h, se_err *rc_out, se_jdec_stop_fn stop, void *sctx)
{
    se_arena a; se_jdec_info ji; se_err rc;
    int ow, oh;
    se_jpeg_dec_info(g_file, g_size > 65536 ? 65536 : g_size, &ji);
    se_jpeg_scaled_size(ji.width, ji.height, scale, &ow, &oh);
    if (info_w) *info_w = ow;
    if (info_h) *info_h = oh;
    if (w < 0) { x0 = 0; y0 = 0; w = ow; h = oh; }
    im->w = w; im->h = h; im->ow = w; im->got = 0;
    im->px = (uint8_t *)malloc((size_t)w * h * 3);
    se_arena_init(&a, arena_mem, arena_sz);
    rc = se_jpeg_decode_window(g_file, g_size > 65536 ? 65536 : g_size, g_size, rd, 0, scale, x0, y0, w, h, stop, sctx, &a, sink, im, &ji);
    if (rc_out) *rc_out = rc;
    return rc == SE_OK && im->got == h;
}

int main(int argc, char **argv)
{
    FILE *f; img_t full, win; int s, fails = 0, ow, oh, i;
    static const int scales[4] = { 1, 2, 4, 8 };
    if (argc < 2) return 2;
    f = fopen(argv[1], "rb"); if (!f) { perror("open"); return 2; }
    fseek(f, 0, SEEK_END); g_size = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    g_file = (uint8_t *)malloc(g_size); if (fread(g_file, 1, g_size, f) != g_size) return 2; fclose(f);
    arena_mem = (uint8_t *)malloc(arena_sz);
    srand(7);

    for (i = 0; i < 4; i++) {
        s = scales[i];
        if (!decode(s, 0, 0, -1, -1, &full, &ow, &oh, 0, 0, 0)) { printf("FAIL: whole picture at 1/%d\n", s); return 1; }
        printf("scale 1/%d: %d x %d\n", s, ow, oh);
        {   /* windows */
            int k;
            for (k = 0; k < 14; k++) {
                int wmax = ow > 700 ? 700 : ow, hmax = oh > 500 ? 500 : oh;
                int w = 1 + rand() % wmax, h = 1 + rand() % hmax;
                int x0 = rand() % (ow - w + 1), y0 = rand() % (oh - h + 1), y;
                if (k == 0) { x0 = 0; y0 = 0; }
                if (k == 1) { x0 = ow - w; y0 = oh - h; }
                if (k == 2) { x0 = 1; y0 = 1; }
                if (!decode(s, x0, y0, w, h, &win, 0, 0, 0, 0, 0)) { printf("FAIL: window %d,%d %dx%d at 1/%d\n", x0, y0, w, h, s); fails++; continue; }
                for (y = 0; y < h; y++)
                    if (memcmp(win.px + (size_t)y * w * 3, full.px + ((size_t)(y0 + y) * ow + x0) * 3, (size_t)w * 3)) { printf("FAIL: window %d,%d %dx%d at 1/%d differs in row %d\n", x0, y0, w, h, s, y); fails++; break; }
                free(win.px);
            }
        }
        if (s > 1) {   /* against a box filter of the full picture */
            img_t one; int ow1, oh1; long sum = 0, n = 0, big = 0; int x, y;
            decode(1, 0, 0, -1, -1, &one, &ow1, &oh1, 0, 0, 0);
            for (y = 0; y < oh - 1; y++) for (x = 0; x < ow - 1; x++) {
                int c, a = 0;
                for (c = 0; c < 3; c++) {
                    int yy, xx, acc = 0, cnt = 0;
                    for (yy = 0; yy < s; yy++) for (xx = 0; xx < s; xx++) {
                        int sx = x * s + xx, sy = y * s + yy;
                        if (sx < ow1 && sy < oh1) { acc += one.px[((size_t)sy * ow1 + sx) * 3 + c]; cnt++; }
                    }
                    a = acc / cnt - full.px[((size_t)y * ow + x) * 3 + c];
                    if (a < 0) a = -a;
                    sum += a; n++; if (a > 24) big++;
                }
            }
            printf("   mean |difference| to a box filter of the full picture: %.2f levels, %.3f %% of the values off by more than 24\n", (double)sum / n, 100.0 * big / n);
            if ((double)sum / n > 8.0) { printf("FAIL: the reduced picture does not look like the full one\n"); fails++; }
            free(one.px);
        }
        free(full.px);
    }
    {   /* the early end */
        img_t t; int ow8, oh8; uint32_t top, bot;
        g_read_max = 0; decode(8, 0, 0, 100, 30, &t, &ow8, &oh8, 0, 0, 0); top = g_read_max; free(t.px);
        g_read_max = 0; decode(8, 0, oh8 - 30, 100, 30, &t, 0, 0, 0, 0, 0); bot = g_read_max; free(t.px);
        printf("bytes of the file that were read: top window %u, bottom window %u of %u\n", top, bot, g_size);
        if (!(top < bot / 4)) { printf("FAIL: the window at the top should end early\n"); fails++; }
    }
    {   /* stop */
        img_t t; se_err rc; int n = 5;
        decode(8, 0, 0, -1, -1, &t, 0, 0, &rc, stop_after, &n);
        printf("stop after 5 rows of blocks: rc = %d (%s), rows out %d\n", rc, se_strerror(rc), t.got);
        if (rc != SE_E_ABORT) { printf("FAIL: no abort\n"); fails++; }
        free(t.px);
    }
    printf(fails ? "dectest: %d FAILED\n" : "dectest: all passed\n", fails);
    return fails != 0;
}
