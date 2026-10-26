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
static int st_lang = 0;                   /* 0 Polski, 1 English, 2 Deutsch, 3 Español, 4 Čeština, 5 Italiano, 6 Français, 7 Magyar, 8 Türkçe */

#define I18N_LIST(X) \
    X(S_TAB_NONE, "Bez efektu", "No effect", "Kein Effekt", "Sin efecto", "Bez efektu", "Nessun effetto", "Sans effet", "Nincs hatás", "Efekt yok") \
    X(S_TAB_C41, "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41", "C-41") \
    X(S_TAB_BW, "BW", "B&W", "S/W", "B/N", "Č/B", "B/N", "N&B", "F/F", "S/B") \
    X(S_TAB_SLIDE, "Slajd", "Slide", "Dia", "Diapositiva", "Diapozitiv", "Slide", "Diapositive", "Dia", "Slayt") \
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
    X(S_LBL_JPG, "JPG z efektem", "JPG with effect", "JPG mit Effekt", "JPG con efecto", "JPG s efektem", "JPG con effetto", "JPG avec effet", "JPG filmhatással", "Efektli JPG") \
    X(S_LBL_LOOK, "Wygląd", "Appearance", "Aussehen", "Aspecto", "Vzhled", "Aspetto", "Apparence", "Megjelenés", "Görünüm") \
    X(S_LBL_LANG, "Język", "Language", "Sprache", "Idioma", "Jazyk", "Lingua", "Langue", "Nyelv", "Dil") \
    X(S_JPG_OFF, "wyłączony", "off", "aus", "desactivado", "vypnuto", "disattivato", "désactivé", "kikapcsolva", "kapalı") \
    X(S_JPG_S, "5,2 Mpix (2784 px)", "5.2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)", "5,2 Mpix (2784 px)") \
    X(S_JPG_M, "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)", "10 Mpix (3861 px)") \
    X(S_JPG_L, "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)", "21 Mpix (5616 px)") \
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
    X(S_ST_BUSY, "obróbka: %d,%d s", "took: %d.%d s", "Dauer: %d,%d s", "tardó: %d,%d s", "trvalo: %d,%d s", "durata: %d,%d s", "durée : %d,%d s", "idő: %d,%d s", "süre: %d,%d s") \
    X(S_LBL_ROLL, "Rolka filmu (zdjęć)", "Film roll (shots)", "Filmrolle (Bilder)", "Carrete (fotos)", "Role filmu (snímků)", "Rullino (scatti)", "Pellicule (vues)", "Filmtekercs (kép)", "Film rulosu (kare)") \
    X(S_ROLL_OFF, "wyłączona", "off", "aus", "desactivado", "vypnuto", "disattivato", "désactivée", "kikapcsolva", "kapalı") \
    X(S_ROLL_TITLE, "Rolki filmu", "Film rolls", "Filmrollen", "Carretes", "Role filmu", "Rullini", "Pellicules", "Filmtekercsek", "Film ruloları") \
    X(S_ROLL_KIND, "Zdjęć na rolkę: %d", "Shots per roll: %d", "Bilder pro Rolle: %d", "Fotos por carrete: %d", "Snímků na roli: %d", "Scatti per rullino: %d", "Vues par pellicule : %d", "Képek tekercsenként: %d", "Rulodaki kare sayısı: %d") \
    X(S_ROLL_OFFNOTE, "Rolka wyłączona w Ustawieniach", "Roll is off in Settings", "Rolle in den Einstellungen aus", "Carrete desactivado en Ajustes", "Role je vypnutá v Nastavení", "Rullino disattivato nelle Impostazioni", "Pellicule désactivée dans Réglages", "A tekercs ki van kapcsolva a Beállításokban", "Rulo Ayarlar'da kapalı") \
    X(S_ROLL_DEVNOTE, "Wywoływanie: zostało %d", "Developing: %d left", "Entwicklung: noch %d", "Revelando: faltan %d", "Vyvolávání: zbývá %d", "Sviluppo: ne mancano %d", "Développement : encore %d", "Előhívás: még %d", "Banyo: %d kaldı") \
    X(S_ROLL_NONE, "Nie ma jeszcze rozpoczętych rolek", "No rolls started yet", "Noch keine Rolle begonnen", "Aún no hay carretes empezados", "Zatím žádná rozpracovaná role", "Nessun rullino iniziato", "Aucune pellicule entamée", "Még nincs megkezdett tekercs", "Henüz başlanmış rulo yok") \
    X(S_ROLL_ROW, "%d / %d  \xC2\xB7  do końca %d  \xC2\xB7  rolek: %d", "%d / %d  \xC2\xB7  %d to go  \xC2\xB7  rolls: %d", "%d / %d  \xC2\xB7  noch %d  \xC2\xB7  Rollen: %d", "%d / %d  \xC2\xB7  faltan %d  \xC2\xB7  carretes: %d", "%d / %d  \xC2\xB7  zbývá %d  \xC2\xB7  rolí: %d", "%d / %d  \xC2\xB7  ne mancano %d  \xC2\xB7  rullini: %d", "%d / %d  \xC2\xB7  encore %d  \xC2\xB7  pellicules : %d", "%d / %d  \xC2\xB7  még %d  \xC2\xB7  tekercs: %d", "%d / %d  \xC2\xB7  kalan %d  \xC2\xB7  rulo: %d") \
    X(S_ROLL_ROW0, "na rolce: %d  \xC2\xB7  rolek: %d", "on the roll: %d  \xC2\xB7  rolls: %d", "auf der Rolle: %d  \xC2\xB7  Rollen: %d", "en el carrete: %d  \xC2\xB7  carretes: %d", "na roli: %d  \xC2\xB7  rolí: %d", "nel rullino: %d  \xC2\xB7  rullini: %d", "sur la pellicule : %d  \xC2\xB7  pellicules : %d", "a tekercsen: %d  \xC2\xB7  tekercs: %d", "rulodaki: %d  \xC2\xB7  rulo: %d") \
    X(S_ROLL_LAB, "wywoływanie: zostało %d  \xC2\xB7  rolek: %d", "developing: %d left  \xC2\xB7  rolls: %d", "Entwicklung: noch %d  \xC2\xB7  Rollen: %d", "revelando: faltan %d  \xC2\xB7  carretes: %d", "vyvolávání: zbývá %d  \xC2\xB7  rolí: %d", "sviluppo: ne mancano %d  \xC2\xB7  rullini: %d", "développement : encore %d  \xC2\xB7  pellicules : %d", "előhívás: még %d  \xC2\xB7  tekercs: %d", "banyo: %d kaldı  \xC2\xB7  rulo: %d") \
    X(S_ROLL_GO, "Wywołaj rolki teraz", "Develop the rolls now", "Rollen jetzt entwickeln", "Revelar los carretes ahora", "Vyvolat role hned", "Sviluppa i rullini ora", "Développer les pellicules", "Tekercsek előhívása most", "Ruloları şimdi banyo et") \
    X(S_ROLL_CLR, "Wyczyść liczniki rolek", "Clear the roll counters", "Rollenzähler löschen", "Borrar contadores de carretes", "Vymazat počítadla rolí", "Azzera i contatori dei rullini", "Effacer les compteurs", "Tekercsszámlálók törlése", "Rulo sayaçlarını temizle") \
    X(S_ROLL_GO_N, "SET: wywołaj (%d)", "SET: develop (%d)", "SET: entwickeln (%d)", "SET: revelar (%d)", "SET: vyvolat (%d)", "SET: sviluppa (%d)", "SET : développer (%d)", "SET: előhív (%d)", "SET: banyo (%d)") \
    X(S_ROLL_CLR_N, "SET: wyczyść (%d)", "SET: clear (%d)", "SET: löschen (%d)", "SET: borrar (%d)", "SET: vymazat (%d)", "SET: azzera (%d)", "SET : effacer (%d)", "SET: töröl (%d)", "SET: temizle (%d)") \
    X(S_ROLL_NOTHING, "brak", "none", "keine", "ninguno", "žádné", "nessuno", "aucun", "nincs", "yok") \
    X(S_MSG_ARM2, "Naciśnij SET jeszcze raz", "Press SET once more", "SET noch einmal drücken", "Pulsa SET otra vez", "Stiskni SET ještě jednou", "Premi ancora SET", "Appuyez encore sur SET", "Nyomd meg a SET-et még egyszer", "SET'e bir kez daha bas") \
    X(S_MSG_GO, "Rolki poszły do wywołania", "Rolls sent to be developed", "Rollen zur Entwicklung geschickt", "Carretes enviados a revelar", "Role poslány k vyvolání", "Rullini inviati allo sviluppo", "Pellicules envoyées au développement", "A tekercsek előhívásra mentek", "Rulolar banyoya gönderildi") \
    X(S_MSG_CLR, "Liczniki rolek wyczyszczone", "Roll counters cleared", "Rollenzähler gelöscht", "Contadores de carretes borrados", "Počítadla rolí vymazána", "Contatori dei rullini azzerati", "Compteurs de pellicules effacés", "A tekercsszámlálók törölve", "Rulo sayaçları temizlendi")

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
static const char *const lang_names[NLANG] = { "Polski", "English", "Deutsch", "Español", "Čeština", "Italiano", "Français", "Magyar", "Türkçe" };

#endif
