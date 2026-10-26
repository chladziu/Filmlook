/* PC test of the film rolls (roll.h): counting, full roll -> lab, the photos that are developed one by one, the file, wrong files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../films.h"
#include "../state.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const short LK[RL_NLK] = { 70, 140, -20, 5, -15, 10, -10, 40, 25 };

static int add(int film, int number) { return rl_add(film, number, 2, "B:/DCIM/100EOS5D/", "IMG_", LK); }

static void reload(void)                          /* what happens at the next start of the camera */
{
    int n = rl_build();
    char keep[sizeof(rl_buf)];
    memcpy(keep, rl_buf, (size_t)n + 1);
    memset(rl_f, 0x55, sizeof rl_f); memset(rl_done, 0x55, sizeof rl_done); rl_n = 77;
    memcpy(rl_buf, keep, (size_t)n + 1);
    rl_parse(n);
    rl_requeue();
}

int main(void)
{
    int i, id, n, v0;
    rl_frame_t fr;

    rl_reset();
    CHECK(rl_n == 0 && rl_count(0, -1) == 0 && !rl_visible());
    st_roll = 0; CHECK(roll_size() == 0 && !rl_visible());
    st_roll = 3; CHECK(roll_size() == 24 && rl_visible());
    st_roll = 1; CHECK(roll_size() == 6);
    st_roll = 4; CHECK(roll_size() == 36);
    st_roll = 99; CHECK(roll_size() == 36);          /* never outside the table */
    st_roll = 1;

    /* photos of two films join their rolls */
    for (i = 0; i < 5; i++) CHECK(add(1, 100 + i) > 0);
    CHECK(add(4, 200) > 0);
    CHECK(rl_count(1, RL_WAIT) == 5 && rl_count(4, RL_WAIT) == 1 && rl_count(0, RL_WAIT) == 6 && rl_count(0, RL_LAB) == 0);
    CHECK(add(0, 1) == 0 && add(BASE_COUNT + 1, 1) == 0);              /* not a base film: no roll */

    /* the 6th photo fills the roll of film 1: only that roll goes to the lab */
    id = add(1, 105); CHECK(id > 0 && rl_count(1, RL_WAIT) == 6);
    n = rl_release(1);
    CHECK(n == 6 && rl_done[0] == 1 && rl_count(1, RL_WAIT) == 0 && rl_count(1, RL_LAB) == 6 && rl_count(4, RL_WAIT) == 1 && rl_done[3] == 0);
    CHECK(rl_release(1) == 0 && rl_done[0] == 1);                      /* nothing more on that roll: no second count */

    /* a new roll of the same film starts at once */
    CHECK(add(1, 106) > 0 && rl_count(1, RL_WAIT) == 1 && rl_count(1, RL_LAB) == 6);

    /* the lab hands the photos out oldest first, each only once */
    CHECK(rl_take(&fr) && fr.number == 100 && fr.film == 1 && fr.mode == 2 && !strcmp(fr.dir, "B:/DCIM/100EOS5D/") && !strcmp(fr.prefix, "IMG_"));
    CHECK(fr.lk[0] == 70 && fr.lk[1] == 140 && fr.lk[2] == -20 && fr.lk[8] == 25);
    CHECK(rl_take(&fr) && fr.number == 101);
    CHECK(rl_count(1, RL_LAB) == 6);                                   /* queued ones still count as being in the lab */
    CHECK(rl_remove(fr.id) == 1 && rl_remove(fr.id) == 0 && rl_count(1, RL_LAB) == 5);

    /* power cycle: waiting photos, the lab, the counters survive; what was queued is to be developed again */
    reload();
    CHECK(rl_count(1, RL_WAIT) == 1 && rl_count(4, RL_WAIT) == 1 && rl_count(1, RL_LAB) == 5 && rl_done[0] == 1 && rl_done[3] == 0);
    CHECK(rl_take(&fr) && fr.number == 100);                           /* the first photo was queued, not finished: it comes again */
    id = fr.id;
    CHECK(fr.lk[0] == 70 && fr.lk[3] == 5 && fr.lk[7] == 40 && !strcmp(fr.dir, "B:/DCIM/100EOS5D/"));
    printf("--- roll file:\n%.*s---\n", rl_build() > 400 ? 400 : rl_build(), rl_buf);

    /* "develop now": every started roll, also the film with one photo; the counters */
    v0 = rl_release(0);
    CHECK(v0 == 2 && rl_done[0] == 2 && rl_done[3] == 1 && rl_count(0, RL_WAIT) == 0);

    /* the list of the panel: the films that have something, in the order of the films */
    rl_entries();
    CHECK(rl_ent_n == 2 && rl_ent[0].film == 1 && rl_ent[0].done == 2 && rl_ent[0].lab == 6 && rl_ent[1].film == 4 && rl_ent[1].lab == 1 && rl_ent[1].done == 1);
    list_build(); st_rolltab = 1; list_build();
    CHECK(lst_n == 4 && lst[0] == RROW0 && lst[1] == RROW0 + 1 && lst[2] == XROW && lst[3] == CROW && cur_tab() == TAB_ROLL);
    { int ids[NTABS], k = tab_order(ids); CHECK(k == TYPE_COUNT + 3 && ids[TYPE_COUNT + 1] == TAB_ROLL && ids[k - 1] == TAB_SET); }
    st_rolltab = 0; list_build();

    /* everything done: the photos leave, the counters stay */
    rl_remove(id);
    while (rl_take(&fr)) rl_remove(fr.id);
    CHECK(rl_n == 0 && rl_done[0] == 2 && st_roll > 0 && rl_visible());   /* (the option is still on) */
    st_roll = 0; CHECK(!rl_visible());
    reload();
    CHECK(rl_done[0] == 2 && rl_done[3] == 1 && rl_n == 0);

    /* clear */
    add(7, 300); add(7, 301);
    CHECK(rl_clear() == 3 && rl_n == 0 && rl_done[0] == 0 && rl_done[3] == 0 && rl_count(0, -1) == 0);

    /* no room: the 121st photo is refused (the caller makes its JPG at once) */
    rl_reset();
    for (i = 0; i < RL_MAXP; i++) CHECK(add(2, 1000 + i) > 0);
    CHECK(add(2, 5000) == 0 && rl_n == RL_MAXP);
    n = rl_build();
    CHECK(n > 0 && n < (int)sizeof(rl_buf) - 1);
    reload();
    CHECK(rl_n == RL_MAXP && rl_count(2, RL_WAIT) == RL_MAXP);

    /* wrong files never crash and never invent photos */
    rl_reset();
    { static const char *bad[] = {
        "", "garbage", "FLROLL 2\nd=Kodak Portra 400;5\n", "FLROLL 1\nd=No Such Film;5\np=0;No Such Film;1;2;B:/;IMG_;1,2,3,4,5,6,7,8,9\n",
        "FLROLL 1\np=0;Kodak Portra 400;1;2;B:/DCIM/;IMG_;1,2,3\n", "FLROLL 1\np=0;Kodak Portra 400;-4;2;B:/DCIM/;IMG_;1,2,3,4,5,6,7,8,9\n",
        "FLROLL 1\np=0;Kodak Portra 400;7;9;B:/DCIM/;IMG_;1,2,3,4,5,6,7,8,9\n", "FLROLL 1\np=x;Kodak Portra 400;7;2\n",
        "FLROLL 1\nd=Kodak Portra 400;99999999999\n" };
      for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++) {
          int l = (int)strlen(bad[i]);
          memcpy(rl_buf, bad[i], (size_t)l + 1);
          rl_parse(l);
          CHECK(rl_n == 0);
      }
      CHECK(rl_done[0] == 9999);                                         /* (the last file: an absurd number is cut, not wrapped) */
    }
    /* a good file with one bad line in the middle: the good lines are kept */
    { static const char *mix = "FLROLL 1\nd=Kodak Portra 400;3\np=0;Kodak Portra 400;11;2;B:/DCIM/100EOS5D/;IMG_;1,2,3,4,5,6,7,8,9\nrubbish line\np=1;Kodak Portra 400;12;3;B:/DCIM/100EOS5D/;IMG_;-1,-2,-3,-4,-5,-6,-7,-8,-9\n";
      int l = (int)strlen(mix);
      memcpy(rl_buf, mix, (size_t)l + 1);
      rl_parse(l);
      CHECK(rl_n == 2 && rl_done[0] == 3 && rl_f[0].number == 11 && rl_f[0].st == RL_WAIT && rl_f[1].number == 12 && rl_f[1].st == RL_LAB && rl_f[1].lk[8] == -9 && rl_f[1].mode == 3);
    }

    /* the setting in the settings file */
    printf(fails ? "\n%d FAILED\n" : "\nall rolls tests passed\n", fails);
    return fails != 0;
}
