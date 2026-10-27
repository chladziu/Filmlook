#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""preset2films.py - turns Lightroom presets (.xmp, crs: attributes) into modules/filmlook/films.h

usage:  preset2films.py <folder with the user's own .xmp presets | -> <EmulsionPresets checkout> <output films.h> [all]
        "-" = leave the user's own presets out (state of v1.6); pass the folder to put the 17 back in.
        Since v0.10 only the CURATED short list (below) is written. "all" as the last argument writes every film as before
        (the old long list, with push / pull variants).

Sources
  1. the user's own presets (the 17 files listed in USER_META)
  2. peva3/EmulsionPresets (MIT licence): Presets/Color-Negative, Presets/Black-White, Presets/Slide
The preset values are copied 1:1 (all develop sliders, HSL, B&W mixer, colour grading, tone curves, calibration, grain,
sharpening / noise reduction, white balance); the module scales them with the "Efekt" slider.
Not copied: the <crs:Look> profile block and crs:Treatment (their identifiers are not verified), crs:Amount, bookkeeping.
Menu structure: Typ (Kolor / Czarno-biały / Slajd) -> Marka -> Film.
"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import films_meta as M

OWN_SLOTS = 0                                               # slots of "Własny" (own presets): none since v0.11 (5 before); set to 5 and add "Własny" to TYPES to bring them back
TYPES = ["Kolor", "Czarno-biały", "Slajd"] + (["Własny"] if OWN_SLOTS else [])   # menu order; "Własny" = no film, only the look sliders
FOLDER_TYPE = {"Color-Negative": 0, "Black-White": 1, "Slide": 2}
BRAND_ORDER = ["Kodak", "Ilford", "Fuji", "Agfa", "Foma", "Adox", "Rollei", "Kentmere", "Bergger", "CineStill",
               "Lomography", "Ferrania", "ORWO", "Tonowanie", "Inne"]
KIND = {0: "Film kolorowy negatywowy", 1: "Film czarno-biały negatywowy", 2: "Film kolorowy slajdowy"}

# --- v0.10: the short list of the most popular films whose looks are clearly different from each other (chosen by measuring
#     the distance between the looks on three test photos, tools/ + notes in the project docs: every pair at least ~4 apart
#     in colour, ~3 in B&W, ~3.8 in slides; Royal Gold is made from Gold 200, see calibration.DERIVE). The order here is the order in the
#     panel. Not in it: expired films, digital film simulations of Fuji cameras, push / pull variants, prints, toning.
CURATED = {
    0: ["Kodak Portra 400", "Kodak Ektar 100", "Kodak Gold 200", "Kodak Royal Gold 400", "Fuji Superia X-TRA 400",
        "Kodak Vision3 500T"],
    1: ["Kodak Tri-X 400", "Ilford HP5 Plus", "Ilford XP2 Super", "Ilford Delta 3200"],
    2: ["Fuji Velvia 50", "Fuji Velvia 100", "Kodachrome 64", "Fuji Astia 100F", "Kodak Ektachrome 100VS"],
}
CURATED_ISO = {"Fuji Provia 100F": 100}               # the preset has no ISO in its name

# --- the user's own presets: (file, brand, short name, ISO, type 0/1/2, Polish description <= 52 chars)
USER_META = [
    ("Kodak_Kodachrome_10.xmp",        "Kodak",  "Kodachrome 10",       10,   2, "Wyciszone czerwienie i zielenie, wyraźny kontrast"),
    ("Kodak_Kodachrome_25.xmp",        "Kodak",  "Kodachrome 25",       25,   2, "Mocno nasycone, jasne i soczyste kolory"),
    ("Kodak_Kodachrome_64_.xmp",       "Kodak",  "Kodachrome 64",       64,   2, "Stonowane kolory, miękkie światła, lekko wyblakłe"),
    ("Kodak_Kodachrome_64_V2.xmp",     "Kodak",  "Kodachrome 64 V2",    64,   2, "Głębokie, nasycone błękity i ciepłe czerwienie"),
    ("Kodak_Kodachrome_200.xmp",       "Kodak",  "Kodachrome 200",      200,  2, "Nasycone czerwienie i zielenie, jasne światła"),
    ("Kodak_Kodachrome_Original.xmp",  "Kodak",  "Kodachrome Original", 0,    2, "Bardzo miękki kontrast, żywe, łagodne barwy"),
    ("Ilford_Delta_100.xmp",           "Ilford", "Delta 100",           100,  1, "Czysty, drobnoziarnisty, szeroka rozpiętość tonów"),
    ("Ilford_Delta_400.xmp",           "Ilford", "Delta 400",           400,  1, "Uniwersalny, wyraźny kontrast, średnie ziarno"),
    ("Ilford_Delta_3200.xmp",          "Ilford", "Delta 3200",          3200, 1, "Miękki kontrast, rozjaśnione cienie"),
    ("Ilford_FP4.xmp",                 "Ilford", "FP4",                 125,  1, "Klasyczny, łagodne światła, drobne ziarno"),
    ("Ilford_HP5.xmp",                 "Ilford", "HP5",                 400,  1, "Reportażowy, miękkie światła, wyraźne ziarno"),
    ("Ilford_HP5__ver._2_.xmp",        "Ilford", "HP5 (ver. 2)",        400,  1, "Mocny kontrast, głęboka czerń, wyraźne ziarno"),
    ("Fuji_Superia_100_.xmp",          "Fuji",   "Superia 100",         100,  0, "Jasne, świeże zielenie i żółcie, czyste kolory"),
    ("Fuji_Superia_400.xmp",           "Fuji",   "Superia 400",         400,  0, "Podniesiona czerń, stonowane zielenie"),
    ("Fuji_Superia_800.xmp",           "Fuji",   "Superia 800",         800,  0, "Wyblakła czerń, chłodne zielenie, wyraźne ziarno"),
    ("Fuji_Superia_1600.xmp",          "Fuji",   "Superia 1600",        1600, 0, "Mocno podniesiona czerń, grube ziarno"),
    ("Fuji_Pro_S.xmp",                 "Fuji",   "Pro S",               160,  0, "Delikatne, neutralne barwy, jasna skóra i zieleń"),
]

# --- attributes carried over, with how the module treats them
#  S = scaled by "Efekt"   G = signed (printed with + / -)   T = technical: only written when Efekt > 0, never scaled
#  R = stored x10 (1 decimal)   H = stored x100 (2 decimals)   C = also gets the "Kontrast" slider
#  A = grain amount (also scaled by the "Ziarno" slider)
COL = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]
ATTRS = [("Exposure2012", "SGH"), ("Contrast2012", "SGC"), ("Highlights2012", "SG"), ("Shadows2012", "SG"),
         ("Whites2012", "SG"), ("Blacks2012", "SG"), ("Clarity2012", "SG"), ("Texture", "SG"), ("Dehaze", "SG"),
         ("Vibrance", "SG"), ("Saturation", "SG"), ("PostCropVignetteAmount", "SG"),
         ("ParametricShadows", "SG"), ("ParametricDarks", "SG"), ("ParametricLights", "SG"), ("ParametricHighlights", "SG"),
         ("ParametricShadowSplit", ""), ("ParametricMidtoneSplit", ""), ("ParametricHighlightSplit", "")]
ATTRS += [("HueAdjustment" + c, "SG") for c in COL]
ATTRS += [("SaturationAdjustment" + c, "SG") for c in COL]
ATTRS += [("LuminanceAdjustment" + c, "SG") for c in COL]
ATTRS += [("GrayMixer" + c, "SG") for c in COL]
for z in ("Shadow", "Midtone", "Highlight", "Global"):
    ATTRS += [("ColorGrade%sHue" % z, ""), ("ColorGrade%sSat" % z, "S"), ("ColorGrade%sLum" % z, "SG")]
ATTRS += [("ColorGradeBlending", ""), ("ColorGradeBalance", "G")]
ATTRS += [("RedHue", "SG"), ("RedSaturation", "SG"), ("GreenHue", "SG"), ("GreenSaturation", "SG"),
          ("BlueHue", "SG"), ("BlueSaturation", "SG"), ("ShadowTint", "SG")]
ATTRS += [("GrainAmount", "SA"), ("GrainSize", ""), ("GrainFrequency", "")]
ATTRS += [("Sharpness", "T"), ("SharpenRadius", "TR"), ("SharpenDetail", "T"), ("SharpenEdgeMasking", "T"),
          ("LuminanceSmoothing", "T"), ("LuminanceNoiseReductionDetail", "T"), ("LuminanceNoiseReductionContrast", "T"),
          ("ColorNoiseReduction", "T"), ("ColorNoiseReductionDetail", "T"), ("ColorNoiseReductionSmoothness", "T")]
ALIAS = {"GrainRoughness": "GrainFrequency"}      # the EmulsionPresets name; Lightroom's own attribute is GrainFrequency
INDEX = {n: i for i, (n, _) in enumerate(ATTRS)}
BOOKKEEPING = ("Supports", "PresetType", "Cluster", "UUID", "RequiresRenditionBehavior", "Name", "Treatment",
               "ProcessVersion", "HasSettings", "Amount", "Version", "WhiteBalance", "Temperature", "Tint",
               "ConvertToGrayscale", "CameraProfile", "ToneCurveName2012", "OverrideLookVignette", "Copyright",
               "ContactInfo", "CameraModelRestriction", "ShowIn")
CURVES = ["ToneCurvePV2012", "ToneCurvePV2012Red", "ToneCurvePV2012Green", "ToneCurvePV2012Blue"]
MAXPTS = 8
PROFILE = {"Adobe Standard": 1, "Camera Standard": 2}      # "Embedded" (JPEG / DNG only) is left out
FLAGBITS = {"S": 1, "G": 2, "T": 4, "R": 8, "C": 16, "A": 32, "H": 64}
BRANDS_STRIP = ["Kodak", "Fujifilm", "Fuji", "Ilford", "Agfa", "Adox", "Foma", "CineStill", "Lomography", "Rollei",
                "Kentmere", "Ferrania", "ORWO", "Bergger"]
BRAND_OF_PREFIX = [("Kodachrome", "Kodak"), ("Ektachrome", "Kodak"), ("Elite Chrome", "Kodak"), ("Kodacolor", "Kodak"),
                   ("Fujicolor", "Fuji"), ("AgfaChrome", "Agfa"), ("Lomochrome", "Lomography")]
IGNORED_DEFAULT = ("SplitToning", "AutoLateralCA", "VignetteAmount", "Defringe", "PostCropVignetteAmount", "RequiresRGBTables")
UNKNOWN = {}


def num(s, flags):
    f = float(s)
    if "H" in flags: return int(round(f * 100))
    if "R" in flags: return int(round(f * 10))
    return int(round(f))


def parse(path):
    t = open(path, encoding="utf-8").read()
    head = re.search(r'<rdf:Description rdf:about=""(.*?)>', t, re.S).group(1)
    a = {}
    for k, v in re.findall(r'crs:(\w+)="([^"]*)"', head):
        a[ALIAS.get(k, k)] = v
    curves = {}
    for m in re.finditer(r'<crs:(ToneCurvePV2012\w*)>\s*<rdf:Seq>(.*?)</rdf:Seq>', t, re.S):
        pts = [tuple(int(float(v)) for v in x.split(",")) for x in re.findall(r'<rdf:li>([^<]*)</rdf:li>', m.group(2))]
        curves[m.group(1)] = pts
    return a, curves


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


import films_i18n as I18N
_DESC_BY_PL = {v[0]: v for v in I18N.DESC.values()}


def lstr(pl, table):
    """{ "pl", "en", "de", "es", "cs", "it", "fr" } for a Polish text; a text without a translation stays Polish in all languages"""
    t = table.get(pl) or (_DESC_BY_PL.get(pl) if table is I18N.DESC else None) or (pl,) * len(I18N.LANGS)
    return "{ " + ", ".join(cstr(x) for x in t) + " }"


def natkey(s):
    return [int(p) if p.isdigit() else p.lower() for p in re.split(r'(\d+)', s)]


def split_brand(name):
    """'Kodak Portra 400 +1' -> ('Kodak', 'Portra 400 +1', full name)"""
    for pre, br in BRAND_OF_PREFIX:
        if name.startswith(pre):
            return br, name
    for b in BRANDS_STRIP:
        if name.startswith(b + " "):
            return ("Fuji" if b == "Fujifilm" else b), name[len(b) + 1:]
    return "Inne", name


def guess_iso(name):
    for tok in re.findall(r'(?<![\w+\-.])(\d{2,4})(?![\w])', name):
        v = int(tok)
        if 25 <= v <= 3200: return v
    return 0


def push_label(f, sh):
    """text of one Push/Pull option (Polish; B&W: push / pull with the exposure index, colour: EV)"""
    iso = f["iso"]
    if f["type"] == 1:
        if sh == 0: return "Normalnie (EI %d)" % iso if iso else "Normalnie"
        ei = (" (EI %d)" % (iso * 2 ** sh if sh > 0 else iso // 2 ** (-sh))) if iso else ""
        return ("Push +%d" % sh if sh > 0 else "Pull %d" % sh) + ei
    if sh == 0: return "Normalnie"
    return "%+d EV %s" % (sh, "prześwietlone" if sh > 0 else "niedoświetlone")


def build_entry(a, curves, brand, name, full, iso, typ, desc, kind, src):
    vals = []
    for i, (n, fl) in enumerate(ATTRS):
        if n in a:
            vals.append((i, num(a[n], fl)))
    for k in a:
        if k in INDEX or k.startswith(BOOKKEEPING): continue
        if k.startswith(IGNORED_DEFAULT):                # Lightroom defaults / unused panels: must be neutral
            if k.endswith(("HueLo", "HueHi")) or a[k] in ("False", "0", "+0", "0.00") : continue
        UNKNOWN.setdefault(k, set()).add(src)
    mono = a.get("ConvertToGrayscale") == "True"
    cv = []
    for c in CURVES:
        pts = curves.get(c, [(0, 0), (255, 255)])
        if len(pts) > MAXPTS: raise SystemExit("%s: curve %s has %d points" % (src, c, len(pts)))
        cv.append(pts)
    return dict(src=src, brand=brand, name=name, full=full, iso=iso, type=typ, desc=desc, kind=kind, mono=mono,
                wb_custom=1 if a.get("WhiteBalance") == "Custom" else 0,
                temp=num(a.get("Temperature", "0"), ""), tint=num(a.get("Tint", "0"), ""),
                profile=PROFILE.get(a.get("CameraProfile", ""), 0), vals=vals, cv=cv)


def main(user_dir, repo_dir, out, mode=""):
    everything = (mode == "all")
    keep_order = {(t, n): i for t, l in CURATED.items() for i, n in enumerate(l)}
    films = []
    # 1. the user's own presets (skipped when the folder is "-")
    for fname, brand, name, iso, typ, desc in (USER_META if user_dir != "-" else []):
        if len(desc) > 52: raise SystemExit("description too long: " + desc)
        a, curves = parse(os.path.join(user_dir, fname))
        mono = a.get("ConvertToGrayscale") == "True"
        if mono != (typ == 1): raise SystemExit("%s: B&W flag disagrees with META" % fname)
        e = build_entry(a, curves, brand, name, brand + " " + name, iso, typ, desc, KIND[typ], fname)
        if e["full"] in M.NEUTRAL_BW:                    # no toning of a grey: shadow / highlight colour-grade saturation (ids 52, 58) = 0
            e["vals"] = [(i, 0 if i in (52, 58) else v) for i, v in e["vals"]]
        films.append(e)
    taken = {(f["type"], f["brand"], f["name"]) for f in films}
    # 2. EmulsionPresets
    cat = json.load(open(os.path.join(repo_dir, "presets.json"), encoding="utf-8"))["presets"]
    wanted = {"Color Negative Film": "Color-Negative", "Black & White Film": "Black-White", "Slide / E-6 Film": "Slide"}
    names = {(p["category"], p["name"]) for p in cat}
    pending = []                      # push / pull variants: (base preset name, shift, suffix, entry)
    for p in cat:
        if p["category"] not in wanted: continue
        pname = p["name"]
        typ = FOLDER_TYPE[wanted[p["category"]]]
        if not everything and (typ, pname) not in keep_order: continue
        a, curves = parse(os.path.join(repo_dir, p["file"]))
        if (a.get("ConvertToGrayscale") == "True") != (typ == 1): raise SystemExit("%s: B&W flag vs folder" % pname)
        brand, name, = split_brand(pname)[0], split_brand(pname)[1]
        if pname in M.BRAND_OVERRIDE: brand = M.BRAND_OVERRIDE[pname]
        if pname in M.NAME_OVERRIDE: name = M.NAME_OVERRIDE[pname]
        iso = M.ISO_OVERRIDE.get(pname, guess_iso(pname))
        if iso == 0: iso = CURATED_ISO.get(pname, 0)
        desc = M.DESC[pname]
        if len(desc) > 52: raise SystemExit("description too long (%d): %s" % (len(desc), desc))
        kind = M.KIND_OVERRIDE.get(pname, KIND[typ])
        if (typ, brand, name) in taken: name += " (EP)"
        taken.add((typ, brand, name))
        full = name if (brand in ("Inne", "Tonowanie") or name.startswith(brand)) else brand + " " + name
        e = build_entry(a, curves, brand, name, full, iso, typ, desc, kind, pname)
        e["order"] = keep_order.get((typ, pname), 0)
        mv = re.match(r'^(.*) ([+-][12])$', pname)
        if everything and mv and (p["category"], mv.group(1)) in names:      # "Kodak Tri-X 400 +1": variant of "Kodak Tri-X 400"
            pending.append((mv.group(1), int(mv.group(2)), mv.group(2), e))
            continue
        films.append(e)

    # 3. five slots for the user's own looks: films without any values - everything comes from the look sliders of the
    #    panel, every slot keeps its own set of sliders (the module stores them in the settings file)
    for k in range(1, OWN_SLOTS + 1):
        films.append(dict(src="(own look %d)" % k, brand="Własne presety", name="Preset %d" % k, full="Własny preset %d" % k,
                          iso=0, type=3, desc="", kind="Własny preset", mono=False, order=k,
                          wb_custom=0, temp=0, tint=0, profile=0, vals=[], cv=[[(0, 0), (255, 255)] for _ in CURVES]))

    # grouping: type -> brand -> film (natural order of the names)
    def border(b): return BRAND_ORDER.index(b) if b in BRAND_ORDER else 99
    if everything: films.sort(key=lambda f: (f["type"], border(f["brand"]), f["brand"], natkey(f["name"])))
    else:          films.sort(key=lambda f: (f["type"], f.get("order", 0)))             # the curated order of the panel
    groups = []
    for i, f in enumerate(films):
        if not groups or groups[-1]["type"] != f["type"] or groups[-1]["brand"] != f["brand"]:
            groups.append(dict(type=f["type"], brand=f["brand"], first=i, n=0))
        groups[-1]["n"] += 1
        f["group"] = len(groups) - 1
    # push / pull variants live after all base films in films[]; they are not listed in the Film row, the
    # "Push/Pull" slider of the base film selects them (popts[] = options of a base film, sorted by shift)
    byname = {f["src"]: f for f in films}
    for f in films: f.update(shift=0, np=0, pfirst=0, var=[])
    for bs, shift, suf, e in pending:
        b = byname[bs]
        if any(x["shift"] == shift for x in b["var"]): raise SystemExit("two variants with shift %d: %s" % (shift, bs))
        e.update(brand=b["brand"], type=b["type"], name=b["name"] + " " + suf, full=b["full"] + " " + suf, iso=b["iso"],
                 kind=b["kind"], group=b["group"], shift=shift, np=0, pfirst=0, var=[])
        b["var"].append(e)
    variants = [e for f in films for e in sorted(f["var"], key=lambda e: e["shift"])]
    allf = films + variants
    for i, f in enumerate(allf): f["idx"] = i
    fulls = [f["full"] for f in allf]
    dup = {x for x in fulls if fulls.count(x) > 1}
    if dup: raise SystemExit("duplicate full names: %s" % dup)
    popts = []
    for f in films:
        if not f["var"]: continue
        opts = sorted([(0, f)] + [(e["shift"], e) for e in f["var"]], key=lambda o: o[0])
        f["pfirst"], f["np"] = len(popts), len(opts)
        for sh, e in opts:
            lab = push_label(f, sh)
            if len(lab) > 22: raise SystemExit("push label too long: " + lab)
            popts.append((sh, lab, e["idx"]))

    import calibration as CAL                         # grain from the manufacturers' figures + measured corrections
    cal_report, cal_missing = CAL.apply(allf, INDEX)
    if cal_missing: print("calibration: no grain figure for:", ", ".join(cal_missing))

    o = []
    w = o.append
    w("/* films.h - GENERATED by tools/preset2films.py. Do not edit by hand.\n"
      " * Sources: the user's own Lightroom presets and peva3/EmulsionPresets (MIT, see THIRD_PARTY_LICENSES.txt).\n"
      " * Every film carries the complete preset (sliders, HSL / B&W mixer, colour grading, tone curves, calibration, grain, ...). */\n")
    w("#ifndef FL_FILMS_H\n#define FL_FILMS_H\n\n")
    w("#define F_SCALE   1     /* scaled by the Efekt slider */\n#define F_SIGN    2     /* printed with + / - */\n"
      "#define F_TECH    4     /* sharpening / noise reduction: only written when Efekt > 0, never scaled */\n"
      "#define F_TENTHS  8     /* stored x10 (SharpenRadius) */\n#define F_CON     16    /* gets the Kontrast slider */\n"
      "#define F_GRAIN   32    /* grain amount: also scaled by the Ziarno slider */\n"
      "#define F_HUND    64    /* stored x100 (Exposure) */\n\n")
    w("typedef struct { const char *name; unsigned char flags; } attr_t;\nstatic const attr_t attr_defs[] = {\n")
    for n, fl in ATTRS:
        w('    { "%s", %d },\n' % (n, sum(FLAGBITS[c] for c in fl)))
    w("};\n#define ATTR_COUNT ((int)(sizeof(attr_defs) / sizeof(attr_defs[0])))\n\n")
    w("typedef struct { unsigned char id; short v; } fv_t;\n\n#define CURVE_MAX %d\n#define FILM_LANGS %d      /* languages of the kind / description texts: see i18n.h */\n" % (MAXPTS, len(I18N.LANGS)))
    w("typedef struct {\n    unsigned short group;     /* index into groups[] */\n"
      "    const char *name;         /* shown in the Film row (UTF-8, Polish letters are converted for the font) */\n"
      "    const char *full;         /* brand + name: goes into the XMP and the keyword */\n"
      "    const char *kind[FILM_LANGS];   /* shown under the name, in every language (pl en de es cs it fr) */\n    const char *desc[FILM_LANGS];   /* short description, same order */\n"
      "    short iso;                /* 0 = not shown */\n"
      "    unsigned char mono, wb_custom, profile;   /* profile: 0 none, 1 Adobe Standard, 2 Camera Standard */\n"
      "    short temp, tint;\n    unsigned char nv;\n    const fv_t *v;\n"
      "    unsigned char nc[4];      /* points of master, red, green, blue tone curve */\n"
      "    unsigned char cv[4][CURVE_MAX][2];\n"
      "    signed char shift;        /* push / pull variant: stops (0 = the base film itself) */\n"
      "    unsigned char np;         /* base film: number of Push/Pull options incl. normal, 0 = none */\n"
      "    unsigned short pfirst;    /* index of its first option in popts[] */\n} film_t;\n\n"
      "typedef struct { signed char shift; const char *label; unsigned short film; } popt_t;   /* film: index into films[] */\n\n")
    w("typedef struct { unsigned char type; const char *brand; unsigned short first, n; } group_t;\n")
    w("typedef struct { const char *name; unsigned char first_group, ngroups; } type_t;\n\n")
    w("static const type_t types[] = {\n")
    for t, tn in enumerate(TYPES):
        gs = [i for i, g in enumerate(groups) if g["type"] == t]
        if gs != list(range(gs[0], gs[0] + len(gs))): raise SystemExit("groups of a type are not contiguous")
        w("    { %s, %d, %d },\n" % (cstr(tn), gs[0], len(gs)))
    w("};\n#define TYPE_COUNT ((int)(sizeof(types) / sizeof(types[0])))\n#define OWN_TYPE %d        /* index of the type \"Własny\" (own presets), -1 = there is none */\n\n" % (TYPES.index("Własny") if "Własny" in TYPES else -1))
    w("static const group_t groups[] = {\n")
    for g in groups:
        w("    { %d, %s, %d, %d },\n" % (g["type"], cstr(g["brand"]), g["first"], g["n"]))
    w("};\n#define GROUP_COUNT ((int)(sizeof(groups) / sizeof(groups[0])))\n\n")
    for i, f in enumerate(allf):
        w("/* %s */\nstatic const fv_t fv_%d[] = {" % (f["src"], i))
        for k, (idx, v) in enumerate(f["vals"]):
            if k % 6 == 0: w("\n    ")
            w("{%d,%d}, " % (idx, v))
        if not f["vals"]: w("\n    {0,0}, /* no values: placeholder, nv = 0 */")
        w("\n};\n")
    w("\nstatic const film_t films[] = {     /* base films first (BASE_COUNT), then the push / pull variants */\n")
    for i, f in enumerate(allf):
        w("    { %d, %s, %s,\n      %s,\n      %s, %d,\n      %d, %d, %d, %d, %d,\n      %d, fv_%d,\n      { %s },\n      {\n"
          % (f["group"], cstr(f["name"]), cstr(f["full"]), lstr(f["kind"], I18N.KIND), lstr(f["desc"], I18N.DESC), f["iso"],
             1 if f["mono"] else 0, f["wb_custom"], f["profile"], f["temp"], f["tint"],
             len(f["vals"]), i, ", ".join(str(len(c)) for c in f["cv"])))
        for c in f["cv"]:
            pts = list(c) + [(0, 0)] * (MAXPTS - len(c))
            w("        { %s },\n" % ", ".join("{%d,%d}" % p for p in pts))
        w("      }, %d, %d, %d },\n" % (f["shift"], f["np"], f["pfirst"]))
    w("};\n\n#define FILM_COUNT ((int)(sizeof(films) / sizeof(films[0])))\n#define BASE_COUNT %d\n\n" % len(films))
    w("static const popt_t popts[] = {\n")
    for sh, lab, fi in popts:
        w("    { %d, %s, %d },\n" % (sh, cstr(lab), fi))
    if not popts: w("    { 0, \"\", 0 },\n")
    w("};\n\n#endif\n")
    open(out, "w", encoding="utf-8").write("".join(o))
    print("films: %d (+ %d push/pull variants)  groups: %d  attrs: %d  push/pull options: %d" % (len(films), len(variants), len(groups), len(ATTRS), len(popts)))
    for t, tn in enumerate(TYPES):
        print("  %-13s %3d films, brands: %s" % (tn, sum(1 for f in films if f["type"] == t),
              ", ".join("%s(%d)" % (g["brand"], g["n"]) for g in groups if g["type"] == t)))
    if UNKNOWN:
        print("WARNING attributes not carried over:", {k: sorted(v)[:3] for k, v in UNKNOWN.items()})
    # for the checker scripts
    json.dump([dict(src=f["src"], full=f["full"], name=f["name"], desc=f["desc"], kind=f["kind"], iso=f["iso"],
                    type=f["type"], brand=f["brand"], mono=f["mono"]) for f in allf],
              open(os.path.splitext(out)[0] + ".index.json", "w", encoding="utf-8"), ensure_ascii=False, indent=0)


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5): raise SystemExit(__doc__)
    main(*sys.argv[1:])
