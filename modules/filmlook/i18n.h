/*
 * i18n.h - the texts of the panel in nine languages: 0 Polish, 1 English, 2 German, 3 Spanish, 4 Czech, 5 Italian, 6 French, 7 Hungarian, 8 Turkish.
 * The language is chosen in the tab "Ustawienia" (row "Język") and kept in the settings file (lang=).
 * Film names stay as they are (they are the names of the films); the kind and the description of a film come from films.h
 * (films[].kind[st_lang], films[].desc[st_lang]).
 *
 * One line per text: X(id, pl, en, de, es, cs, it, fr, hu, tr). The serif font (serif_data.h) has all the letters that are used here
 * (the letters of all nine languages); tools/make_serif_font.py draws them.
 * Texts with a number inside keep the same %d in every language.
 */
#ifndef I18N_H
#define I18N_H

#define NLANG 9
static int st_lang = 0;                   /* 0 polski, 1 English, 2 Deutsch, 3 Español, 4 Čeština, 5 Italiano, 6 Français, 7 Magyar, 8 Türkçe */

#define I18N_LIST(X) \
    X(S_TAB_NONE, "Bez efektu", "No effect", "Kein Effekt", "Sin efecto", "Bez efektu", "Nessun effetto", "Sans effet", "Nincs hatás", "Efekt yok") \
    X(S_TAB_C41, "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41") \
    X(S_TAB_BW, "BW", "B&W", "S/W", "B/N", "Č/B", "B/N", "N&B", "F/F", "S/B") \
    X(S_TAB_SLIDE, "Slajd", "Slide", "Dia", "Diapositiva", "Diapozitiv", "Slide", "Diapositive", "Dia", "Slayt") \
    X(S_TAB_OWN, "Własny", "Custom", "Eigene", "Personal", "Vlastní", "Personali", "Perso", "Saját", "Özel") \
    X(S_TAB_SET, "Ustawienia", "Settings", "Einstellungen", "Ajustes", "Nastavení", "Impostazioni", "Réglages", "Beállítások", "Ayarlar") \
    X(S_SL_EFFECT, "Efekt", "Effect", "Effekt", "Efecto", "Efekt", "Effetto", "Effet", "Hatás", "Etki") \
    X(S_SL_GRAIN, "Ziarno", "Grain", "Korn", "Grano", "Zrno", "Grana", "Grain", "Szemcse", "Tane") \
    X(S_SL_CONTR, "Kontrast", "Contrast", "Kontrast", "Contraste", "Kontrast", "Contrasto", "Contraste", "Kontraszt", "Kontrast") \
    X(S_SL_HAL, "Halacja", "Halation", "Halation", "Halación", "Halace", "Alone", "Halation", "Halo", "Halasyon") \
    X(S_SL_FADE, "Matowość", "Matte", "Matt", "Mate", "Matnost", "Opaco", "Mat", "Matt", "Mat") \
    X(S_SL_WARM, "Ciepło", "Warmth", "Wärme", "Calidez", "Teplo", "Calore", "Chaleur", "Melegség", "Sıcaklık") \
    X(S_SL_SHAD, "Cienie", "Shadows", "Schatten", "Sombras", "Stíny", "Ombre", "Ombres", "Árnyékok", "Gölgeler") \
    X(S_SL_HIGH, "Światła", "Highlights", "Lichter", "Luces", "Světla", "Luci", "Lumières", "Fények", "Işıklar") \
    X(S_SL_SAT, "Nasycenie", "Saturation", "Sättigung", "Saturación", "Sytost", "Saturazione", "Saturation", "Telítettség", "Doygunluk") \
    X(S_SL_VIG, "Winieta", "Vignette", "Vignette", "Viñeta", "Vinětace", "Vignettatura", "Vignetage", "Vignettálás", "Vinyet") \
    X(S_HINT_KEYS, "pokrętło: zmiana\tjoystick: wiersz\tSET: zapis\tkosz: zamknij", "dial: change\tjoystick: row\tSET: save\ttrash: close", "Rad: ändern\tJoystick: Zeile\tSET: speichern\tLöschtaste: schließen", "rueda: cambiar\tjoystick: fila\tSET: guardar\tpapelera: cerrar", "kolečko: změna\tjoystick: řádek\tSET: uložit\tkoš: zavřít", "ghiera: modifica\tjoystick: riga\tSET: salva\tcestino: chiudi", "molette : modifier\tjoystick : ligne\tSET : enregistrer\tcorbeille : fermer", "tárcsa: módosít\tjoystick: sor\tSET: mentés\tkuka: bezár", "kadran: değiştir\tjoystick: satır\tSET: kaydet\tçöp kutusu: kapat") \
    X(S_OFF_NOTE, "Efekt filmu wyłączony", "Film effect off", "Filmeffekt aus", "Efecto de película desactivado", "Efekt filmu je vypnutý", "Effetto pellicola disattivato", "Effet pellicule désactivé", "A filmhatás ki van kapcsolva", "Film efekti kapalı") \
    X(S_SET_NOTE1, "Dotyczą wszystkich filmów", "They apply to all films", "Gelten für alle Filme", "Valen para todas las películas", "Platí pro všechny filmy", "Valgono per tutti i film", "Valables pour tous les films", "Minden filmre érvényesek", "Tüm filmler için geçerli") \
    X(S_SET_NOTE2, "JPG, miejsce zapisu, wygląd i język", "JPG, save location, appearance and language", "JPG, Speicherort, Aussehen und Sprache", "JPG, ubicación, aspecto e idioma", "JPG, umístění, vzhled a jazyk", "JPG, posizione, aspetto e lingua", "JPG, emplacement, apparence et langue", "JPG, mentés helye, megjelenés és nyelv", "JPG, kayıt yeri, görünüm ve dil") \
    X(S_LBL_JPG, "JPG z efektem", "JPG with effect", "JPG mit Effekt", "JPG con efecto", "JPG s efektem", "JPG con effetto", "JPG avec effet", "JPG filmhatással", "Efektli JPG") \
    X(S_LBL_PLACE, "Miejsce JPG", "JPG location", "JPG-Ablage", "Ubicación JPG", "Umístění JPG", "Posizione JPG", "Emplacement JPG", "JPG helye", "JPG konumu") \
    X(S_LBL_LOOK, "Wygląd", "Appearance", "Aussehen", "Aspecto", "Vzhled", "Aspetto", "Apparence", "Megjelenés", "Görünüm") \
    X(S_LBL_LANG, "Język", "Language", "Sprache", "Idioma", "Jazyk", "Lingua", "Langue", "Nyelv", "Dil") \
    X(S_JPG_OFF, "wyłączony", "off", "aus", "desactivado", "vypnuto", "disattivato", "désactivé", "kikapcsolva", "kapalı") \
    X(S_JPG_HALF, "połowa (2808 px)", "half (2808 px)", "halb (2808 px)", "mitad (2808 px)", "polovina (2808 px)", "metà (2808 px)", "moitié (2808 px)", "fél (2808 px)", "yarım (2808 px)") \
    X(S_JPG_FULL, "pełny (5616 px)", "full (5616 px)", "voll (5616 px)", "completo (5616 px)", "plná (5616 px)", "intera (5616 px)", "pleine (5616 px)", "teljes (5616 px)", "tam (5616 px)") \
    X(S_PL_FOLDER, "folder FILMLOOK", "folder FILMLOOK", "Ordner FILMLOOK", "carpeta FILMLOOK", "složka FILMLOOK", "cartella FILMLOOK", "dossier FILMLOOK", "FILMLOOK mappa", "FILMLOOK klasörü") \
    X(S_PL_REPL, "podmień JPG aparatu", "replace camera JPG", "Kamera-JPG ersetzen", "sustituir JPG de la cámara", "nahradit JPG foťáku", "sostituisci JPG originale", "remplacer le JPG de l'appareil", "gép JPG-jének cseréje", "kamera JPG'sini değiştir") \
    X(S_PL_PREV, "JPG i podgląd RAW", "JPG and RAW preview", "JPG und RAW-Vorschau", "JPG y vista previa RAW", "JPG a náhled RAW", "JPG e anteprima RAW", "JPG et aperçu RAW", "JPG és RAW előnézet", "JPG ve RAW önizleme") \
    X(S_TH_CREAM, "krem", "cream", "Creme", "crema", "krémový", "crema", "crème", "krém", "krem") \
    X(S_TH_NIGHT, "noc", "night", "Nacht", "noche", "noční", "notte", "nuit", "éjszaka", "gece") \
    X(S_RST_FILM, "Domyślne tego filmu", "Defaults for this film", "Standardwerte dieses Films", "Predeterminado: esta película", "Výchozí hodnoty tohoto filmu", "Valori predefiniti del film", "Réglages d'origine du film", "A film alapértékei", "Bu filmin varsayılanları") \
    X(S_RST_ALL, "Domyślne wszystkich filmów", "Defaults for all films", "Standardwerte aller Filme", "Predeterminado: todas las películas", "Výchozí hodnoty všech filmů", "Valori predefiniti di tutti i film", "Réglages d'origine de tous les films", "Az összes film alapértékei", "Tüm filmlerin varsayılanları") \
    X(S_RST_N, "SET: przywróć (%d)", "SET: restore (%d)", "SET: zurücksetzen (%d)", "SET: restaurar (%d)", "SET: obnovit (%d)", "SET: ripristina (%d)", "SET : rétablir (%d)", "SET: visszaállít (%d)", "SET: geri yükle (%d)") \
    X(S_RST_NONE, "bez zmian", "unchanged", "unverändert", "sin cambios", "beze změn", "invariato", "inchangé", "változatlan", "değişiklik yok") \
    X(S_RST_CONF, "SET jeszcze raz: potwierdź", "SET again: confirm", "SET nochmal: bestätigen", "SET otra vez", "SET znovu: potvrdit", "SET ancora: conferma", "SET encore : confirmer", "SET még egyszer: megerősít", "SET tekrar: onayla") \
    X(S_MSG_SAVED, "Zapisano", "Saved", "Gespeichert", "Guardado", "Uloženo", "Salvato", "Enregistré", "Mentve", "Kaydedildi") \
    X(S_MSG_ALRDY, "Ustawienia są już domyślne", "Settings are already the defaults", "Einstellungen sind schon Standard", "Los ajustes ya son los predeterminados", "Nastavení je už výchozí", "Le impostazioni sono già predefinite", "Les réglages sont déjà ceux par défaut", "A beállítások már az alapértékek", "Ayarlar zaten varsayılan") \
    X(S_MSG_RSTF, "Przywrócono ustawienia domyślne filmu", "Film defaults restored", "Standardwerte des Films wiederhergestellt", "Valores predeterminados de la película restaurados", "Výchozí hodnoty filmu obnoveny", "Predefiniti del film ripristinati", "Réglages du film rétablis", "A film alapértékei visszaállítva", "Filmin varsayılanları geri yüklendi") \
    X(S_MSG_RSTA, "Przywrócono ustawienia domyślne wszystkich filmów", "Defaults for all films restored", "Standardwerte aller Filme wiederhergestellt", "Valores predeterminados de todas las películas restaurados", "Výchozí hodnoty všech filmů obnoveny", "Predefiniti di tutti i film ripristinati", "Réglages de tous les films rétablis", "Az összes film alapértékei visszaállítva", "Tüm filmlerin varsayılanları geri yüklendi") \
    X(S_MSG_ARM, "Wszystkie filmy: naciśnij SET jeszcze raz", "All films: press SET once more", "Alle Filme: SET noch einmal drücken", "Todas las películas: pulsa SET otra vez", "Všechny filmy: stiskni SET ještě jednou", "Tutti i film: premi ancora SET", "Tous les films : appuyez encore sur SET", "Minden film: nyomd meg a SET-et még egyszer", "Tüm filmler: SET'e bir kez daha bas") \
    X(S_ST_WAIT, "czekam: aparat", "waiting: camera", "warte: Kamera", "espera: cámara", "čekám na foťák", "in attesa", "attente : boîtier", "várok a gépre", "kamera meşgul") \
    X(S_ST_WRITE, "zapis na kartę", "writing to card", "schreibe: Karte", "guardando", "zápis na kartu", "scrittura scheda", "écrit sur carte", "írás a kártyára", "karta yazılıyor") \
    X(S_ST_QUEUE, "kolejka JPG: %d", "JPG queue: %d", "JPG wartet: %d", "cola JPG: %d", "fronta JPG: %d", "coda JPG: %d", "file JPG : %d", "JPG sor: %d", "JPG sırası: %d") \
    X(S_ST_BUSY, "obróbka: %d,%d s", "took: %d.%d s", "Dauer: %d,%d s", "tardó: %d,%d s", "trvalo: %d,%d s", "durata: %d,%d s", "durée : %d,%d s", "idő: %d,%d s", "süre: %d,%d s")

enum {
#define X(id, pl, en, de, es, cs, it, fr, hu, tr) id,
    I18N_LIST(X)
#undef X
    S_COUNT
};

static const char *const i18n_t[S_COUNT][NLANG] = {
#define X(id, pl, en, de, es, cs, it, fr, hu, tr) { pl, en, de, es, cs, it, fr, hu, tr },
    I18N_LIST(X)
#undef X
};

#define TR(id) (i18n_t[(id)][st_lang])

/* the names of the languages are always written in the language itself */
static const char *const lang_names[NLANG] = { "polski", "English", "Deutsch", "Español", "Čeština", "Italiano", "Français", "Magyar", "Türkçe" };

#endif
