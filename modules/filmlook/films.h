/* films.h - film table. All numbers are ORIGINAL rough starting values (to be calibrated).
 * con/hi/sh/wh/bl/sat/vib = Lightroom sliders at 100% effect
 * g/gs/gr = grain amount / size / roughness; sh_h.. = split toning; curve = 5 tone-curve points */
#ifndef FL_FILMS_H
#define FL_FILMS_H

typedef struct {
    const char *name;
    const char *kind;      /* shown under the name (UTF-8, Polish letters are converted for the font) */
    const char *desc;      /* short description */
    int iso;
    int mono;
    int con, hi, sh, wh, bl, sat, vib;
    int g, gs, gr;
    int sh_h, sh_s, hi_h, hi_s;
    int curve[5][2];
} film_t;

static const film_t films[] = {
    { "Kodak Gold 200", "Film kolorowy negatywowy", "Ciepłe, złote tony, miękki kontrast", 200, 0,
      10, -15, 15, -5, 10, 5, 10,
      25, 30, 50,
      40, 8, 45, 12,
      { {0,12}, {64,70}, {128,134}, {192,198}, {255,248} } },

    { "Kodak Portra 400", "Film kolorowy negatywowy", "Łagodne odcienie skóry, delikatne pastelowe kolory", 400, 0,
      -5, -25, 20, 0, 8, -8, 5,
      20, 28, 45,
      190, 5, 40, 8,
      { {0,10}, {64,68}, {128,130}, {192,196}, {255,250} } },

    { "Kodak Ektar 100", "Film kolorowy negatywowy", "Intensywne, nasycone kolory, bardzo drobne ziarno", 100, 0,
      20, -10, 5, 5, -5, 25, 10,
      12, 22, 40,
      0, 0, 40, 6,
      { {0,3}, {64,56}, {128,130}, {192,205}, {255,255} } },

    { "Fujifilm Velvia 50", "Film kolorowy slajdowy", "Intensywne kolory, wysoki kontrast", 50, 0,
      30, -5, 0, 8, -10, 40, 10,
      10, 20, 35,
      220, 6, 30, 6,
      { {0,0}, {64,50}, {128,128}, {192,214}, {255,255} } },

    { "Ilford HP5 Plus 400", "Film czarno-biały negatywowy", "Klasyczne ziarno, duża rozpiętość tonalna", 400, 1,
      15, -10, 10, 0, -5, 0, 0,
      45, 30, 60,
      0, 0, 0, 0,
      { {0,6}, {64,60}, {128,128}, {192,200}, {255,250} } },

    { "Kodak Tri-X 400", "Film czarno-biały negatywowy", "Mocny kontrast i wyraźne ziarno", 400, 1,
      30, -10, 5, 5, -10, 0, 0,
      55, 35, 65,
      0, 0, 0, 0,
      { {0,0}, {64,52}, {128,128}, {192,208}, {255,255} } },
};

#define FILM_COUNT ((int)(sizeof(films) / sizeof(films[0])))

#endif
