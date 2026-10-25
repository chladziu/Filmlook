# -*- coding: utf-8 -*-
"""films_i18n.py - the kind and the short description of every curated film in seven languages.
Order of the languages everywhere: pl, en, de, es, cs, it, fr, hu, tr (same as modules/filmlook/i18n.h).
The Polish texts are the ones of films_meta.py (DESC / KIND); preset2films.py checks that they are the same.
"""
LANGS = ["pl", "en", "de", "es", "cs", "it", "fr", "hu", "tr"]

KIND = {
  "Film kolorowy negatywowy": ("Film kolorowy negatywowy", "Colour negative film", "Farbnegativfilm", "Película negativa de color",
      "Barevný negativní film", "Pellicola colore negativa", "Film négatif couleur"),
  "Film czarno-biały negatywowy": ("Film czarno-biały negatywowy", "Black-and-white negative film", "Schwarzweiß-Negativfilm",
      "Película negativa en blanco y negro", "Černobílý negativní film", "Pellicola negativa bianco e nero", "Film négatif noir et blanc"),
  "Film kolorowy slajdowy": ("Film kolorowy slajdowy", "Colour slide film", "Farbdiafilm", "Película de diapositivas en color",
      "Barevný diapozitivní film", "Pellicola diapositiva a colori", "Film diapositive couleur"),
  "Film kinowy kolorowy": ("Film kinowy kolorowy", "Colour cinema film", "Farbfilm fürs Kino", "Película de cine en color",
      "Barevný kinofilm", "Pellicola cinematografica a colori", "Film cinéma couleur"),
  "Film czarno-biały (proces C-41)": ("Film czarno-biały (proces C-41)", "Black-and-white film (C-41 process)",
      "Schwarzweißfilm (C-41-Prozess)", "Película en blanco y negro (proceso C-41)", "Černobílý film (proces C-41)",
      "Pellicola bianco e nero (processo C-41)", "Film noir et blanc (procédé C-41)"),
  "Własny preset": ("Własny preset", "Own preset", "Eigenes Preset", "Preset propio", "Vlastní preset", "Preset personale", "Preset personnel"),
}

# film (as in films_meta.DESC) -> descriptions in pl, en, de, es, cs, it, fr
DESC = {
  "Kodak Portra 400": ("Ciepły, miękki, ceniony za skórę i pastele", "Warm, soft, loved for skin tones and pastels",
      "Warm, weich, beliebt für Haut und Pastelltöne", "Cálido y suave, ideal para piel y tonos pastel",
      "Teplý, jemný, oblíbený pro pleť a pastely", "Caldo e morbido, amato per pelle e pastelli", "Chaud et doux, apprécié pour les peaux et pastels"),
  "Kodak Ektar 100": ("Mocno nasycony, bardzo drobne ziarno, kontrast", "Vivid colour, very fine grain, high contrast",
      "Kräftige Farben, sehr feines Korn, Kontrast", "Color intenso, grano muy fino, mucho contraste",
      "Sytý, velmi jemné zrno, silný kontrast", "Molto saturo, grana finissima, contrasto", "Très saturé, grain très fin, contrasté"),
  "Kodak Gold 200": ("Ciepły, złoty klasyk, żywe żółcie", "Warm golden classic, lively yellows",
      "Warmer, goldener Klassiker, lebhafte Gelbtöne", "Clásico cálido y dorado, amarillos vivos",
      "Teplý zlatý klasik, živé žluté tóny", "Classico caldo e dorato, gialli vivaci", "Classique chaud et doré, jaunes éclatants"),
  "Kodak Royal Gold 400": ("Bogate nasycenie, drobniejsze ziarno niż Gold", "Rich saturation, finer grain than Gold",
      "Satte Farben, feineres Korn als Gold", "Saturación rica, grano más fino que Gold",
      "Bohatá sytost, jemnější zrno než Gold", "Saturazione ricca, grana più fine del Gold", "Saturation riche, grain plus fin que le Gold"),
  "Fuji Superia X-TRA 400": ("Wyraziste zielenie, chłodne cienie, uniwersalny", "Punchy greens, cool shadows, all-rounder",
      "Kräftiges Grün, kühle Schatten, Allrounder", "Verdes intensos, sombras frías, versátil",
      "Výrazná zeleň, chladné stíny, univerzální", "Verdi decisi, ombre fredde, versatile", "Verts marqués, ombres froides, polyvalent"),
  "Kodak Vision3 500T": ("Kino 500T w C-41: zmiana barw, kinowe ziarno", "Cinema 500T in C-41: colour shift, cinematic grain",
      "Kino 500T in C-41: Farbstich, filmisches Korn", "Cine 500T en C-41: cambio de color, grano de cine",
      "Kinofilm 500T v C-41: posun barev, filmové zrno", "Cinema 500T in C-41: dominanti, grana da cinema", "Cinéma 500T en C-41: dominantes, grain de cinéma"),
  "Kodak Tri-X 400": ("Król reportażu: charakterystyczne ziarno, kontrast", "King of reportage: signature grain, contrast",
      "König der Reportage: markantes Korn, Kontrast", "Rey del reportaje: grano característico, contraste",
      "Král reportáže: výrazné zrno, kontrast", "Re del reportage: grana tipica, contrasto", "Roi du reportage: grain typique, contraste"),
  "Ilford HP5 Plus": ("Klasyczny reportażowy 400, wyraźne ziarno", "Classic documentary 400, visible grain",
      "Klassischer Reportagefilm 400, deutliches Korn", "Clásico de reportaje 400, grano marcado",
      "Klasika reportáže 400, patrné zrno", "Classico da reportage 400, grana evidente", "Classique du reportage 400, grain marqué"),
  "Ilford XP2 Super": ("C-41, ziarno barwnikowe, gładkie światła", "C-41, dye-cloud grain, smooth highlights",
      "C-41, Farbstoffkorn, glatte Lichter", "C-41, grano de colorante, luces suaves",
      "C-41, barvivové zrno, hladká světla", "C-41, grana da coloranti, luci morbide", "C-41, grain de colorant, hautes lumières douces"),
  "Ilford Delta 3200": ("Grube ziarno, podniesiona czerń, niski kontrast", "Coarse grain, lifted blacks, low contrast",
      "Grobes Korn, angehobenes Schwarz, wenig Kontrast", "Grano grueso, negros elevados, poco contraste",
      "Hrubé zrno, zvednutá černá, nízký kontrast", "Grana grossa, neri sollevati, poco contrasto", "Gros grain, noirs relevés, faible contraste"),
  "Fuji Velvia 50": ("Ekstremalne nasycenie, głębokie zielenie, kontrast", "Extreme saturation, deep greens, contrast",
      "Extreme Sättigung, tiefes Grün, Kontrast", "Saturación extrema, verdes profundos, contraste",
      "Extrémní sytost, hluboká zeleň, kontrast", "Saturazione estrema, verdi profondi, contrasto", "Saturation extrême, verts profonds, contraste"),
  "Fuji Velvia 100": ("Łagodniejsza niż Velvia 50, cieplejsza", "Milder than Velvia 50, warmer",
      "Milder als Velvia 50, wärmer", "Más suave que la Velvia 50, más cálida",
      "Mírnější než Velvia 50, teplejší", "Più morbida della Velvia 50, più calda", "Plus douce que la Velvia 50, plus chaude"),
  "Kodachrome 64": ("Klasyczny slajd: głębokie błękity, ciepłe czerwienie", "Classic slide: deep blues, warm reds",
      "Klassisches Dia: tiefes Blau, warmes Rot", "Diapositiva clásica: azules profundos, rojos cálidos",
      "Klasický diapozitiv: hluboká modř, teplá červená", "Diapositiva classica: blu profondi, rossi caldi", "Diapositive classique: bleus profonds, rouges chauds"),
  "Fuji Astia 100F": ("Miękki, portretowy, neutralna skóra", "Soft, portrait-friendly, neutral skin",
      "Weich, porträtgeeignet, neutrale Haut", "Suave, de retrato, piel neutra",
      "Jemný, portrétní, neutrální pleť", "Morbido, da ritratto, pelle neutra", "Doux, pour le portrait, peau neutre"),
  "Kodak Ektachrome 100VS": ("Wersja Vivid: mocne czerwienie i błękity", "Vivid version: strong reds and blues",
      "Vivid-Version: kräftiges Rot und Blau", "Versión Vivid: rojos y azules intensos",
      "Verze Vivid: silné červené a modré tóny", "Versione Vivid: rossi e blu intensi", "Version Vivid: rouges et bleus intenses"),
}

# Hungarian and Turkish are added here so that the tuples above stay readable; the language order is LANGS
for _k, _t in {'Film kolorowy negatywowy': ('Színes negatív film', 'Renkli negatif film'), 'Film czarno-biały negatywowy': ('Fekete-fehér negatív film', 'Siyah-beyaz negatif film'), 'Film kolorowy slajdowy': ('Színes diafilm', 'Renkli slayt filmi'), 'Film kinowy kolorowy': ('Színes mozifilm', 'Renkli sinema filmi'), 'Film czarno-biały (proces C-41)': ('Fekete-fehér film (C-41 eljárás)', 'Siyah-beyaz film (C-41 işlemi)'), 'Własny preset': ('Saját preset', 'Özel preset')}.items():
    KIND[_k] = KIND[_k] + _t
for _k, _t in {'Kodak Portra 400': ('Meleg, lágy, bőrtónusokhoz és pasztellekhez', 'Sıcak, yumuşak; ten ve pastel tonlar için'), 'Kodak Ektar 100': ('Élénk színek, nagyon finom szemcse, kontraszt', 'Canlı renkler, çok ince tane, yüksek kontrast'), 'Kodak Gold 200': ('Meleg, aranyló klasszikus, élénk sárgák', 'Sıcak, altın klasik; canlı sarılar'), 'Kodak Royal Gold 400': ('Gazdag telítettség, finomabb szemcse a Goldnál', "Zengin doygunluk, Gold'dan daha ince tane"), 'Fuji Superia X-TRA 400': ('Erős zöldek, hűvös árnyékok, sokoldalú', 'Belirgin yeşiller, soğuk gölgeler, çok yönlü'), 'Kodak Vision3 500T': ('Mozifilm 500T C-41-ben: színeltolódás, filmes szemcse', "C-41'de sinema 500T: renk kayması, sinema tanesi"), 'Kodak Tri-X 400': ('A riport királya: markáns szemcse, kontraszt', 'Foto muhabirliğinin kralı: belirgin tane, kontrast'), 'Ilford HP5 Plus': ('Klasszikus riportfilm 400, látható szemcse', 'Klasik foto muhabiri filmi 400, belirgin tane'), 'Ilford XP2 Super': ('C-41, festékszemcse, sima fények', 'C-41, boya tanesi, yumuşak parlak alanlar'), 'Ilford Delta 3200': ('Durva szemcse, emelt feketék, alacsony kontraszt', 'Kaba tane, yükseltilmiş siyahlar, düşük kontrast'), 'Fuji Velvia 50': ('Extrém telítettség, mély zöldek, kontraszt', 'Aşırı doygunluk, derin yeşiller, kontrast'), 'Fuji Velvia 100': ('Szelídebb a Velvia 50-nél, melegebb', "Velvia 50'den yumuşak, daha sıcak"), 'Kodachrome 64': ('Klasszikus dia: mély kékek, meleg vörösek', 'Klasik slayt: derin maviler, sıcak kırmızılar'), 'Fuji Astia 100F': ('Lágy, portrébarát, semleges bőrtónus', 'Yumuşak, portreye uygun, nötr ten'), 'Kodak Ektachrome 100VS': ('Vivid változat: erős vörösek és kékek', 'Vivid sürüm: güçlü kırmızılar ve maviler')}.items():
    DESC[_k] = DESC[_k] + _t
assert all(len(v) == len(LANGS) for v in list(KIND.values()) + list(DESC.values()))

# French typography: a space before the colon (in the French strings only: the last of the seven first columns)
def _fr(s):
    return s.replace(": ", " : ")
for _d in (KIND, DESC):
    for _k, _t in list(_d.items()):
        _l = list(_t); _l[6] = _fr(_l[6]); _d[_k] = tuple(_l)
