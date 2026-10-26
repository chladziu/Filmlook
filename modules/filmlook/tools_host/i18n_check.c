/* PC check of the texts of the panel in every language: do they fit the places they are drawn in, and does the font have all their letters?
 * Same geometry and same font data as the camera (panel.h / serif.h). Exit code 0 = all fits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VERSION "v0.11"
#include "../films.h"
#include "../state.h"
#include "../panel.h"

static int fails = 0;
static const char *const lname[NLANG] = { "pl", "en", "de", "es", "cs", "it", "fr", "hu", "tr" };

static void check_glyphs(const sf_font *f, const char *fname, const char *s, const char *what)
{
    const char *p = s;
    while (*p) {
        int c = sf_utf8(&p);
        if (c == '\t') c = ' ';
        if (!sf_find(f, c)) { printf("FAIL [%s] %s: no letter U+%04X in the font %s: \"%s\"\n", lname[st_lang], what, c, fname, s); fails++; }
    }
}

#define FIT(font, fname, str, maxw, what) do { \
    const char *s_ = (str); int w_; \
    check_glyphs(&(font), fname, s_, what); \
    w_ = sf_width(&(font), s_); \
    if (w_ > (maxw)) { printf("FAIL [%s] %s: %d px > %d px: \"%s\"\n", lname[st_lang], what, w_, (maxw), s_); fails++; } \
} while (0)

int main(void)
{
    int l, i, j;
    const int W = M_R - M_L;
    for (l = 0; l < NLANG; l++) {
        char tmp[200];
        st_lang = l;

        /* the tabs: one row, all together */
        {
            int tot = 0;
            for (i = 0; i < NTABS; i++) { check_glyphs(&sf_xsb, "xsb", tab_text(i), "tab (bold)"); check_glyphs(&sf_xs, "xs", tab_text(i), "tab"); tot += sf_width(&sf_xsb, tab_text(i)) + TAB_GAP; }
            if (tot > W) { printf("FAIL [%s] tabs: %d px > %d px\n", lname[l], tot, W); fails++; }
        }
        /* the big heading (name of the tab Ustawienia / Bez efektu) */
        FIT(sf_big, "big", TR(S_TAB_SET), W - 100, "heading Ustawienia");
        FIT(sf_big, "big", TR(S_TAB_NONE), W - 100, "heading Bez efektu");
        /* sliders: label left of the bar */
        for (j = 0; j < NSL; j++) if (j != 3) FIT(sf_sml, "sml", TR(S_SL_EFFECT + j), BAR_X - 12 - M_L - 6, "slider label");
        /* choices: label + value */
        {
            const int lab[3] = { S_LBL_JPG, S_LBL_LOOK, S_LBL_LANG };
            int k;
            for (k = 0; k < 3; k++) FIT(sf_sml, "sml", TR(lab[k]), CH_X - M_L - 6, "settings label");
            for (k = 0; k < 4; k++) {
                snprintf(tmp, sizeof tmp, "\xE2\x80\xB9  %s  \xE2\x80\xBA", TR(S_JPG_OFF + k)); FIT(sf_sml, "sml", tmp, M_R3 - CH_X - 4, "JPG option");
            }
            for (k = 0; k < 2; k++) { snprintf(tmp, sizeof tmp, "\xE2\x80\xB9  %s  \xE2\x80\xBA", TR(S_TH_CREAM + k)); FIT(sf_sml, "sml", tmp, M_R3 - CH_X - 4, "theme option"); }
        }
        for (i = 0; i < NLANG; i++) { snprintf(tmp, sizeof tmp, "\xE2\x80\xB9  %s  \xE2\x80\xBA", lang_names[i]); FIT(sf_sml, "sml", tmp, M_R3 - CH_X - 4, "language name"); }
        /* the reset lines: the name and what SET does must fit in one line with a gap between */
        {
            const int nm[2] = { S_RST_FILM, S_RST_ALL };
            int k;
            for (k = 0; k < 2; k++) {
                int w1 = sf_width(&sf_sml, TR(nm[k]));
                snprintf(tmp, sizeof tmp, TR(S_RST_N), 12);
                if (w1 + 30 + sf_width(&sf_sml, tmp) > M_R3 - M_L) { printf("FAIL [%s] reset line %d: %d + %d px\n", lname[l], k, w1, sf_width(&sf_sml, tmp)); fails++; }
                check_glyphs(&sf_sml, "sml", TR(nm[k]), "reset name"); check_glyphs(&sf_sml, "sml", tmp, "reset text");
                if (w1 + 30 + sf_width(&sf_sml, TR(S_RST_NONE)) > M_R3 - M_L) { printf("FAIL [%s] reset line (none) %d\n", lname[l], k); fails++; }
            }
            if (sf_width(&sf_sml, TR(S_RST_ALL)) + 30 + sf_width(&sf_sml, TR(S_RST_CONF)) > M_R3 - M_L) { printf("FAIL [%s] reset-all line when asking\n", lname[l]); fails++; }
            check_glyphs(&sf_sml, "sml", TR(S_RST_CONF), "reset question");
        }
        /* the hint line (justified: the tabs are the gaps) */
        FIT(sf_xxs, "xxs", TR(S_HINT_KEYS), W - 40, "key hint");
        /* messages in the hint line */
        { const int ms[] = { S_MSG_SAVED, S_MSG_ALRDY, S_MSG_RSTF, S_MSG_RSTA, S_MSG_ARM }; for (i = 0; i < 5; i++) FIT(sf_xxs, "xxs", TR(ms[i]), W, "message"); }
        /* the JPG status at the top left (the title starts right of it) */
        FIT(sf_xxs, "xxs", TR(S_ST_WAIT), 132, "status wait");
        FIT(sf_xxs, "xxs", TR(S_ST_WRITE), 132, "status write");
        snprintf(tmp, sizeof tmp, TR(S_ST_QUEUE), 4);   FIT(sf_xxs, "xxs", tmp, 132, "status queue");
        snprintf(tmp, sizeof tmp, TR(S_ST_BUSY), 88, 8); FIT(sf_xxs, "xxs", tmp, 132, "status time");
        /* notes under the heading */
        FIT(sf_xs, "xs", TR(S_OFF_NOTE), W, "no-effect note");
        /* every film: the kind line (kind, ISO, number) and the description; the smaller size is the fall-back */
        for (i = 0; i < BASE_COUNT; i++) {
            const film_t *f = &films[i];
            char k1[160];
            if (f->iso > 0) snprintf(k1, sizeof k1, "%s \xC2\xB7 ISO %d   \xC2\xB7   %d / %d", f->kind[l], f->iso, 12, 12);
            else            snprintf(k1, sizeof k1, "%s   \xC2\xB7   %d / %d", f->kind[l], 12, 12);
            check_glyphs(&sf_xs, "xs", f->kind[l], "kind"); check_glyphs(&sf_xxs, "xxs", f->kind[l], "kind");
            if (sf_width(&sf_xxs, k1) > W) { printf("FAIL [%s] kind line of %s: %d px\n", lname[l], f->full, sf_width(&sf_xxs, k1)); fails++; }
            check_glyphs(&sf_xs, "xs", f->desc[l], "desc"); check_glyphs(&sf_xxs, "xxs", f->desc[l], "desc");
            if (sf_width(&sf_xxs, f->desc[l]) > W) { printf("FAIL [%s] description of %s: %d px\n", lname[l], f->full, sf_width(&sf_xxs, f->desc[l])); fails++; }
            if (sf_width(&sf_xs, f->desc[l]) > W || sf_width(&sf_xs, k1) > W) printf("note [%s] %s: the smaller size is used (desc %d, kind line %d px)\n", lname[l], f->full, sf_width(&sf_xs, f->desc[l]), sf_width(&sf_xs, k1));
        }
    }
    for (i = 0; i < BASE_COUNT; i++) {                  /* the names of the films: the large size, or the smaller one when it is too wide */
        int w = sf_width(&sf_big, films[i].name);
        check_glyphs(&sf_big, "big", films[i].name, "film name"); check_glyphs(&sf_mid, "mid", films[i].name, "film name");
        if (w > W - 100 && sf_width(&sf_mid, films[i].name) > W - 100) { printf("FAIL film name too wide: %s\n", films[i].name); fails++; }
        if (w > W - 100) printf("note: %s uses the smaller name size\n", films[i].name);
    }
    printf(fails ? "i18n check: %d FAILED\n" : "i18n check: all texts fit, all letters exist (%d languages)\n", fails ? fails : NLANG);
    return fails != 0;
}
