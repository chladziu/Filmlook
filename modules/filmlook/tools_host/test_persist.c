/* PC test of the settings file (persist.h) and of the sliders of every film (state.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../films.h"
#include "../state.h"
#include "../persist.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int find(const char *name)
{
    int i;
    for (i = 0; i < BASE_COUNT; i++) if (!strcmp(films[i].full, name)) return i + 1;
    printf("no film %s\n", name); exit(2);
}

static void choose(int f) { st_film = f; switch_set(); }

static void reload(void)                         /* what happens at the next start of the camera */
{
    int n = cfg_build();
    char keep[sizeof(cfg_buf)];
    memcpy(keep, cfg_buf, (size_t)n + 1);
    memset(film_v, 0x55, sizeof film_v);        /* nothing may survive in RAM */
    st_film = 0; st_push = 0; st_jpg = 0;
    st_strength = 7; st_grain = 7; st_contrast = 7; st_fade = 7; st_warm = 7; st_shadow = 7; st_high = 7; st_sat = 7; st_vig = 7; st_hal = 7;
    memcpy(cfg_buf, keep, (size_t)n + 1);
    cfg_parse(n);
}

int main(void)
{
    int a = find("Kodak Portra 400"), b = find("Ilford HP5 Plus"), c = find("Fuji Velvia 50"), i, n;
    int own1 = own_film(1), own5 = own_film(5);

    memo_init(); sets_init();
    cfg_parse(0);                                   /* no file */
    CHECK(st_film == 0 && st_strength == 100 && st_grain == 100 && st_hal == 0);

    choose(a); st_strength = 70; st_grain = 140; st_contrast = 20; st_hal = 30;
    CHECK(mod_n() == 4);
    choose(b); CHECK(st_strength == 100 && st_grain == 100 && st_contrast == 0 && mod_n() == 0);   /* the other film is untouched */
    st_warm = -15; st_sat = 40;
    choose(a); CHECK(st_strength == 70 && st_grain == 140 && st_contrast == 20 && st_hal == 30 && st_warm == 0);
    choose(b); CHECK(st_warm == -15 && st_sat == 40);
    choose(own1); CHECK(is_own(own1) && st_strength == 100); st_grain = 55; st_fade = 10;
    CHECK(mod_n() == 2);                            /* the effect strength of an own preset does not count */
    choose(b); st_jpg = 2; st_push = 0;

    n = cfg_build();
    CHECK(n > 0 && strstr(cfg_buf, "film=Ilford HP5 Plus\n") && strstr(cfg_buf, "mod=Kodak Portra 400;70,140,20,30,0,0,0,0,0,0\n"));
    CHECK(!strstr(cfg_buf, "Fuji Velvia 50"));  /* nobody touched it: no line */
    printf("--- settings file:\n%s---\n", cfg_buf);

    reload();
    CHECK(st_film == b && st_warm == -15 && st_sat == 40 && st_jpg == 2);
    choose(a); CHECK(st_strength == 70 && st_grain == 140 && st_contrast == 20 && st_hal == 30);
    choose(c); CHECK(st_strength == 100 && st_grain == 100 && mod_n() == 0);
    choose(own1); CHECK(st_grain == 55 && st_fade == 10 && st_strength == 100);
    choose(own5); CHECK(mod_n() == 0);

    /* the reset: only this film, the others keep their changes */
    choose(a); CHECK(reset_film() == 4); CHECK(mod_n() == 0 && st_strength == 100 && st_hal == 0);
    n = cfg_build(); CHECK(!strstr(cfg_buf, "mod=Kodak Portra 400"));
    CHECK(strstr(cfg_buf, "mod=Ilford HP5 Plus;"));
    choose(own1); CHECK(reset_film() == 2); CHECK(st_grain == 100 && st_fade == 0 && st_strength == 100);

    /* limits: a damaged file never gives values outside the sliders */
    snprintf(cfg_buf, sizeof(cfg_buf), "film=Kodak Portra 400\nmod=Kodak Portra 400;500,900,-900,900,900,-900,900,-900,900,900\nmod=No such film;1,2,3,4,5,6,7,8,9,10\nmod=Ilford HP5 Plus;1,2\n");
    n = (int)strlen(cfg_buf); memset(film_v, 0, sizeof film_v); sets_init(); st_film = 0; cur_slot = 0; cfg_parse(n);
    CHECK(st_film == a && st_strength == 100 && st_grain == 200 && st_contrast == -50 && st_hal == 100 && st_fade == 100 && st_warm == -100 &&
          st_shadow == 100 && st_high == -100 && st_sat == 100 && st_vig == 100);
    choose(b); CHECK(mod_n() == 0);                 /* the cut line is ignored */

    /* a file of the versions before 0.7 */
    snprintf(cfg_buf, sizeof(cfg_buf), "film=Kodak Portra 400\npush=0\nstrength=80\ngrain=120\ncontrast=10\nfade=5\nwarm=-10\nshadow=0\nhigh=0\nsat=15\nvig=20\nhal=25\njpg=1\n"
             "own1=60,5,10,0,-5,0,0,0,30\nown2=100,0,0,0,0,0,0,0,0\nown3=\n");
    n = (int)strlen(cfg_buf); sets_init(); st_film = 0; cur_slot = 0; cfg_parse(n);
    CHECK(st_film == a && st_strength == 80 && st_grain == 120 && st_contrast == 10 && st_fade == 5 && st_warm == -10 && st_sat == 15 &&
          st_vig == 20 && st_hal == 25 && st_jpg == 1);
    choose(own1); CHECK(st_grain == 60 && st_contrast == 5 && st_hal == 10 && st_warm == -5 && st_vig == 30 && st_strength == 100);
    choose(b); CHECK(mod_n() == 0);                 /* the old global values belong to the chosen film only */

    /* every film can be changed and comes back */
    for (i = 1; i <= BASE_COUNT; i++) { choose(i); st_grain = 100 + (i % 20) * 5; st_vig = i % 7 * 5; }
    reload();
    for (i = 1; i <= BASE_COUNT; i++) { choose(i); CHECK(st_grain == 100 + (i % 20) * 5 && st_vig == i % 7 * 5); }
    printf("settings of all %d films: %d bytes of 12288\n", BASE_COUNT, cfg_build());

    printf(fails ? "FAILED %d\n" : "persist test: all passed\n", fails);
    (void)own5;
    return fails != 0;
}
