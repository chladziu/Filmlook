/* PC test: gcc -Wall -Wextra -o test_xmp test_xmp.c && ./test_xmp && xmllint --noout out.xmp */
#include <stdio.h>
#include "xmp.h"
int main(void)
{
    static char buf[4096];
    int i, bad = 0;
    for (i = 0; i < FILM_COUNT; i++) {
        int n = build_xmp(buf, sizeof(buf), &films[i], 100, 100, 0);
        printf("%-22s %d bytes\n", films[i].name, n);
        if (n < 0) bad = 1;
        if (i == 0) { FILE *f = fopen("out.xmp", "w"); fputs(buf, f); fclose(f); }
    }
    if (build_xmp(buf, 64, &films[0], 100, 100, 0) != -1) { puts("overflow check FAILED"); bad = 1; }
    return bad;
}
