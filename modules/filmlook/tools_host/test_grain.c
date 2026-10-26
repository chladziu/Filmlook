/* PC test of the plate grain of the engine (engine/look.c): a flat grey picture through the look with a plate.
 * Checks: the grain has the rms that was asked for, it does not dip or peak at the borders of the cells (the blend keeps the rms),
 * a mono plate gives the same grain in all three channels, a colour plate does not, the same seed gives the same picture and
 * another seed another one. */
#define SE_HOSTED 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../engine/look.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define W 1400
#define H 700
static uint8_t pic[H][W][3];
static int8_t plate[SE_GP_BYTES];
static se_look lk;
static uint8_t arena_mem[4 << 20];

static int emit(void *ctx, const uint8_t *row, int w)
{
    int *y = (int *)ctx;
    memcpy(pic[*y], row, (size_t)w * 3);
    (*y)++;
    return 0;
}

static void make_plate(int mono, unsigned seed)
{
    int i;
    unsigned s = seed;
    for (i = 0; i < SE_GP_W * SE_GP_H; i++) {
        int c, base;
        s = s * 1664525u + 1013904223u; base = (int)((s >> 16) % 111) - 55;
        for (c = 0; c < 3; c++) {
            int v = base;
            if (!mono) { s = s * 1664525u + 1013904223u; v = (int)((s >> 16) % 111) - 55; }
            plate[i * 3 + c] = (int8_t)v;
        }
    }
}

static void run(unsigned stride, unsigned seed)
{
    se_arena ar;
    se_look_run *r;
    int y = 0, i, v;
    memset(&lk, 0, sizeof lk);
    for (i = 0; i < 256; i++) lk.map[0][i] = lk.map[1][i] = lk.map[2][i] = (uint8_t)i;
    lk.sat = 256;
    lk.grain = 50; lk.gp = plate; lk.gp_stride = stride; lk.gp_amp = 1200; lk.gp_seed = seed;
    for (v = 0; v < 256; v++) lk.gp_tone[v] = (uint8_t)((v * (255 - v)) >> 6);
    se_arena_init(&ar, arena_mem, sizeof arena_mem);
    CHECK(se_look_begin(&r, &lk, W, &ar) == SE_OK);
    for (i = 0; i < H; i++) {
        static uint8_t row[W * 3];
        int x;
        for (x = 0; x < W * 3; x++) row[x] = 128;
        CHECK(se_look_rows(r, row, 1, emit, &y) == SE_OK);
    }
    CHECK(se_look_end(r, emit, &y) == SE_OK);
    CHECK(y == H);
}

static double rms(int x0, int x1, int y0, int y1, int ch)
{
    double s = 0, m = 0; long n = 0; int x, y;
    for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) { double d = pic[y][x][ch] - 128.0; s += d * d; m += d; n++; }
    return sqrt(s / n - (m / n) * (m / n));
}

int main(void)
{
    static uint8_t keep[H][W][3];
    unsigned strides[3] = { 65536u, 124000u, 262144u };       /* 1.0, 1.9, 4.0 texels per pixel */
    int k;
    for (k = 0; k < 3; k++) {
        unsigned st = strides[k];
        double all, lo = 1e9, hi = 0;
        int cellx = (int)(SE_GP_W * 65536.0 / st), celly = (int)(SE_GP_H * 65536.0 / st), b, x0, y0;
        make_plate(0, 77);
        run(st, 5);
        all = rms(0, W, 0, H, 1);
        CHECK(all > 3.0 && all < 12.0);                       /* about 32 * 1200 * 254 / 2^20 = 9.3 at the middle tone */
        /* the rms in a narrow band on the cell border and in the middle of a cell: the same within 12 % */
        for (b = 0; b < 3; b++) {
            double v;
            x0 = (b + 1) * cellx; if (x0 + 6 > W) break;
            v = rms(x0 - 3, x0 + 3, 0, H, 1);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        for (b = 0; b < 3; b++) {
            double v;
            y0 = (b + 1) * celly; if (y0 + 6 > H) break;
            v = rms(0, W, y0 - 3, y0 + 3, 1);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        printf("stride %.2f: rms %.2f, on the borders of cells %.2f .. %.2f\n", st / 65536.0, all, lo, hi);
        CHECK(lo > all * 0.85 && hi < all * 1.15);
    }
    /* colour plate: the channels differ; mono plate: they do not */
    make_plate(0, 77); run(124000u, 5);
    { int x, y, same = 0; for (y = 0; y < H; y++) for (x = 0; x < W; x++) if (pic[y][x][0] == pic[y][x][1] && pic[y][x][1] == pic[y][x][2]) same++; CHECK(same < W * H / 4); }
    memcpy(keep, pic, sizeof pic);
    run(124000u, 5);
    CHECK(memcmp(keep, pic, sizeof pic) == 0);                /* the same seed: the same picture */
    run(124000u, 6);
    CHECK(memcmp(keep, pic, sizeof pic) != 0);                /* another seed: another one */
    make_plate(1, 77); run(124000u, 5);
    { int x, y, same = 0; for (y = 0; y < H; y++) for (x = 0; x < W; x++) if (pic[y][x][0] == pic[y][x][1] && pic[y][x][1] == pic[y][x][2]) same++; CHECK(same == W * H); }
    printf(fails ? "grain test: %d FAILED\n" : "grain test: all passed\n", fails);
    return fails != 0;
}
