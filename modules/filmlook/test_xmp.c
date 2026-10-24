/* PC test: gcc -Wall -Wextra -o test_xmp test_xmp.c && ./test_xmp [outdir]
 * writes <outdir>/<n>_<strength>.xmp for every film at 100 % and 50 % and 0 %; run xmllint --noout on them */
#include <stdio.h>
#include <stdlib.h>
#include "xmp.h"
int main(int argc, char **argv)
{
    static char buf[16384];
    const char *dir = argc > 1 ? argv[1] : ".";
    static const int st[3] = { 100, 50, 0 };
    int i, k, bad = 0, maxn = 0;
    for (i = 0; i < FILM_COUNT; i++) {
        for (k = 0; k < 3; k++) {
            char path[256]; FILE *fp;
            int n = build_xmp(buf, sizeof(buf), &films[i], st[k], 100, 0);
            if (n < 0) { printf("%s: build error\n", films[i].full); bad = 1; continue; }
            if (n > maxn) maxn = n;
            snprintf(path, sizeof(path), "%s/%02d_%d.xmp", dir, i, st[k]);
            fp = fopen(path, "w"); fputs(buf, fp); fclose(fp);
        }
        printf("%-26s brand %d  %d values\n", films[i].full, films[i].brand, films[i].nv);
    }
    printf("largest sidecar: %d bytes\n", maxn);
    if (build_xmp(buf, 64, &films[0], 100, 100, 0) != -1) { puts("overflow check FAILED"); bad = 1; }
    return bad;
}
