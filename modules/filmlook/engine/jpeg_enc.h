/*
 * jpeg_enc.h - baseline JPEG encoder, streaming and integer-only.
 *
 * Writes 4:2:0 baseline sequential JPEG (the chroma planes at half width and height: half the encoding work of 4:4:4,
 * and the picture does not show it). Rows are pushed in 16-line strips
 * so the encoder never holds more than one MCU row, which is what lets the
 * whole pipeline run in a band rather than a frame.
 *
 * Integer throughout: the IJG "islow" DCT, and reciprocal multiplies in
 * place of divides, because the ARM946E-S has neither an FPU nor a divide
 * instruction.
 */
#ifndef SE_JPEG_ENC_H
#define SE_JPEG_ENC_H

#include "se.h"

/* Sink for encoded bytes. Returns 0 on success. */
typedef int (*se_jpeg_sink)(void *ctx, const uint8_t *data, size_t len);

#define SE_JPEG_MCU 16
/* bytes of the strip the caller supplies */
#define SE_JPEG_STRIP_BYTES(width) ((size_t)SE_JPEG_MCU * (size_t)(width) * 3)

/*
 * Metadata written as an EXIF APP1 block. Two reasons this matters: the
 * shot keeps its exposure data, and ColorSpace=sRGB is tagged explicitly.
 * An untagged JPEG gets colour-managed by whatever the viewer guesses,
 * which silently changes the picture.
 */
typedef struct {
    const char *make;
    const char *model;
    const char *software;    /* we put the film stock name here */
    const char *datetime;    /* "YYYY:MM:DD HH:MM:SS" */
    uint32_t exp_num, exp_den;   /* exposure time as a fraction of a second; den 0 to omit */
    uint32_t fn_num, fn_den;     /* f-number as a fraction (e.g. 28 / 10); den 0 to omit */
    int   iso;               /* 0 to omit */
    int   orientation;       /* EXIF orientation 1..8, 0 = 1 (the camera rotation of the source) */
} se_jpeg_exif;

typedef struct {
    se_jpeg_sink sink;
    void        *ctx;

    int width, height;
    int quality;

    uint8_t  qt[2][64];        /* natural order    */
    uint32_t qrecip[2][64];    /* Q16 reciprocals  */

    /* bit assembly */
    uint32_t bitbuf;
    int      bitcnt;

    int      dcpred[3];

    /* one MCU row of input, 16 lines of RGB8 */
    uint8_t *strip;            /* SE_JPEG_STRIP_BYTES(width), supplied by the caller */
    int      strip_rows;

    int      rows_done;
    int      failed;

    /* small output staging buffer so the sink sees few, large writes */
    uint8_t  out[4096];
    int      outpos;

    uint8_t  exifbuf[1024];
} se_jpeg;

/*
 * This struct is ~5 KB. Allocate it from an arena, never on the stack:
 * Magic Lantern task stacks are small and an overflow there corrupts
 * Canon's memory rather than faulting cleanly.
 */

/*
 * `strip` must point at SE_JPEG_STRIP_BYTES(width) bytes of scratch owned by the caller
 * and must stay valid until se_jpeg_end().
 */
se_err se_jpeg_begin(se_jpeg *j, int width, int height, int quality,
                     uint8_t *strip, const se_jpeg_exif *exif,
                     se_jpeg_sink sink, void *ctx);

/* Push `nrows` rows of 8-bit interleaved RGB. Any count is accepted. */
se_err se_jpeg_write_rows(se_jpeg *j, const uint8_t *rgb, int nrows);

se_err se_jpeg_end(se_jpeg *j);

#endif /* SE_JPEG_ENC_H */
