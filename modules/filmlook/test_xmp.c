/* PC test: gcc -Wall -Wextra -o test_xmp test_xmp.c && ./test_xmp [outdir]
 * writes <outdir>/<n>_<variant>.xmp for every film: Efekt 100 / 50 / 0, plus one with every look slider at its extreme
 * (+) and one at the other extreme (-); run xmllint --noout on them (it also catches a duplicated attribute) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xmp.h"

static int count_str(const char *s, const char *needle)
{
    int n = 0; size_t l = strlen(needle);
    while ((s = strstr(s, needle)) != NULL) { n++; s += l; }
    return n;
}

int main(int argc, char **argv)
{
    static char buf[16384];
    const char *dir = argc > 1 ? argv[1] : ".";
    static const look_t V[5] = {
        { 100, 100,   0,   0,    0,    0,    0,    0,   0 },
        {  50, 100,   0,   0,    0,    0,    0,    0,   0 },
        {   0, 100,   0,   0,    0,    0,    0,    0,   0 },
        { 100, 200,  50, 100,  100,  100,  100,  100, 100 },
        { 100,   0, -50,   0, -100, -100, -100, -100,   0 },
    };
    static const char *const vn[5] = { "100", "50", "0", "plus", "minus" };
    int i, k, bad = 0, maxn = 0;
    for (i = 0; i < FILM_COUNT; i++) {
        for (k = 0; k < 5; k++) {
            char path[256]; FILE *fp;
            int n = build_xmp(buf, sizeof(buf), &films[i], &V[k]);
            if (n < 0) { printf("%s: build error (%s)\n", films[i].full, vn[k]); bad = 1; continue; }
            if (n > maxn) maxn = n;
            /* every attribute must appear at most once */
            { int a; for (a = 0; a < ATTR_COUNT; a++) {
                char key[80]; snprintf(key, sizeof(key), "\n   crs:%s=\"", attr_defs[a].name);
                if (count_str(buf, key) > 1) { printf("%s: attribute %s twice (%s)\n", films[i].full, attr_defs[a].name, vn[k]); bad = 1; } } }
            snprintf(path, sizeof(path), "%s/%03d_%s.xmp", dir, i, vn[k]);
            fp = fopen(path, "w"); fputs(buf, fp); fclose(fp);
        }
        if (i < BASE_COUNT)
            printf("%-34s %-14s %-10s %d values\n", films[i].full, types[groups[films[i].group].type].name, groups[films[i].group].brand, films[i].nv);
    }
    printf("largest sidecar: %d bytes\n", maxn);
    if (build_xmp(buf, 64, &films[0], &V[0]) != -1) { puts("overflow check FAILED"); bad = 1; }
    return bad;
}
