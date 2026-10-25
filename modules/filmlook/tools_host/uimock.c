/* PC test of the panel screen: uimock <out.ppm> [film full name] [key=value ...]
 * keys: row top push strength grain contrast hal fade warm shadow high sat vig jpg msg=<text>
 * The picture is made by the very same panel.h / serif.h that the camera runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VERSION "v0.6"
#include "../films.h"
#include "../state.h"
#include "../panel.h"

static unsigned char fb[720 * 480];

int main(int argc, char **argv)
{
    int i, r, c;
    FILE *f;
    if (argc < 2) return 1;
    memo_init();
    sets_init();
    for (i = 2; i < argc; i++) {
        const char *a = argv[i];
        int v = 0;
        const char *eq = strchr(a, '=');
        if (!eq) {
            int k;
            for (k = 0; k < BASE_COUNT; k++) if (!strcmp(films[k].full, a)) st_film = k + 1;
            continue;
        }
        v = atoi(eq + 1);
        if (!strncmp(a, "row=", 4)) row = v;
        else if (!strncmp(a, "top=", 4)) top = v;
        else if (!strncmp(a, "push=", 5)) st_push = v;
        else if (!strncmp(a, "strength=", 9)) st_strength = v;
        else if (!strncmp(a, "grain=", 6)) st_grain = v;
        else if (!strncmp(a, "contrast=", 9)) st_contrast = v;
        else if (!strncmp(a, "hal=", 4)) st_hal = v;
        else if (!strncmp(a, "fade=", 5)) st_fade = v;
        else if (!strncmp(a, "warm=", 5)) st_warm = v;
        else if (!strncmp(a, "shadow=", 7)) st_shadow = v;
        else if (!strncmp(a, "high=", 5)) st_high = v;
        else if (!strncmp(a, "sat=", 4)) st_sat = v;
        else if (!strncmp(a, "vig=", 4)) st_vig = v;
        else if (!strncmp(a, "jpg=", 4)) st_jpg = v;
    }
    fix_push();
    cur_set = set_of(st_film);
    list_build();
    pal_build();
    ui_fb = fb; ui_pitch = 720;
    draw_static();
    paint_rows();
    for (i = 2; i < argc; i++) if (!strncmp(argv[i], "msg=", 4)) draw_hint(argv[i] + 4);
    f = fopen(argv[1], "wb");
    fprintf(f, "P6\n720 480\n255\n");
    for (r = 0; r < 480 * 720; r++) {
        int idx = fb[r] - PAL_BASE;
        if (idx < 0 || idx >= PAL_COUNT) idx = 0;
        for (c = 0; c < 3; c++) fputc(pal_rgb[idx][c], f);
    }
    fclose(f);
    return 0;
}
