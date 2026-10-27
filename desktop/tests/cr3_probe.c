/* cr3_probe.c - the CR3 decoder of the program on its own (src/fd_cr3.h): prints what it found in the file and writes the sensor values
 * it decoded (16 bit, little endian, the whole sensor array) to a file, so that tests/run_tests.py can compare them with the exact array
 * that tests/crxgen.py coded, and run damaged files through it (built with the address and undefined behaviour sanitizers where the
 * compiler has them).
 *
 * usage: cr3_probe <file.cr3> [<output.u16>]
 * exit code: 0 decoded, 1 not a CR3 that the program decodes (parse refused), 2 the sensor data is damaged, 3 the file can not be read */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fd_core.h"

int main(int argc, char **argv)
{
    FILE *f;
    size_t n, i;
    uint8_t *buf;
    fr_info ri;
    uint16_t *raw;
    int rc;
    if (argc < 2 || !(f = fopen(argv[1], "rb"))) return 3;
    fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(n ? n : 1);
    if (!buf || fread(buf, 1, n, f) != n) { fclose(f); free(buf); return 3; }
    fclose(f);
    rc = fr_parse_cr3(buf, n, &ri);
    if (rc) { printf("parse rc=%d\n", rc); free(buf); return 1; }
    printf("model=%s orient=%d raw %dx%d picture %d,%d %dx%d cfa=%d%d%d%d black=%d,%d,%d,%d white=%d wb=%d,%d,%d levels=%d tile %dx%d\n",
           ri.model, ri.orient, ri.rw, ri.rh, ri.x0, ri.y0, ri.w, ri.h, ri.cfa[0], ri.cfa[1], ri.cfa[2], ri.cfa[3],
           ri.black[0], ri.black[1], ri.black[2], ri.black[3], ri.white, ri.wb[0], ri.wb[1], ri.wb[2], ri.crx.levels, ri.crx.tile_w, ri.crx.tile_h);
    raw = (uint16_t *)calloc((size_t)ri.rw * (size_t)ri.rh, 2);
    if (!raw) { free(buf); return 3; }
    rc = fr_decode_cr3(buf, n, &ri, raw);
    printf("decode rc=%d\n", rc);
    if (!rc && argc > 2 && (f = fopen(argv[2], "wb"))) {
        for (i = 0; i < (size_t)ri.rw * (size_t)ri.rh; i++) { fputc(raw[i] & 255, f); fputc(raw[i] >> 8, f); }
        fclose(f);
    }
    free(raw); free(buf);
    return rc ? 2 : 0;
}
