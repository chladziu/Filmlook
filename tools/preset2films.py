#!/usr/bin/env python3
"""preset2films.py - turns Lightroom preset files (.xmp, crs: attributes) into modules/filmlook/films.h

usage:  preset2films.py <folder with the .xmp presets> <output films.h>

The preset values are copied 1:1 (all develop sliders, HSL, B&W mixer, tone curves, calibration, grain,
sharpening / noise reduction, white balance).  The module scales them with the "Efekt" slider.
Brand, ISO, film type and the Polish description come from the META table below - edit it when you add films.
"""
import re, sys, os

# --- (file name part, brand, short name, ISO (0 = unknown), type, Polish description <= 52 chars)
#     type: 'neg' colour negative, 'slide' colour slide, 'bw' black & white negative
#     order here = order in the menu; brands appear in BRANDS order
BRANDS = ["Kodak", "Ilford", "Fuji"]
META = [
    ("Kodak_Kodachrome_10.xmp",        "Kodak",  "Kodachrome 10",       10,   "slide", "Wyciszone czerwienie i zielenie, wyraźny kontrast"),
    ("Kodak_Kodachrome_25.xmp",        "Kodak",  "Kodachrome 25",       25,   "slide", "Mocno nasycone, jasne i soczyste kolory"),
    ("Kodak_Kodachrome_64_.xmp",       "Kodak",  "Kodachrome 64",       64,   "slide", "Stonowane kolory, miękkie światła, lekko wyblakłe"),
    ("Kodak_Kodachrome_64_V2.xmp",     "Kodak",  "Kodachrome 64 V2",    64,   "slide", "Głębokie, nasycone błękity i ciepłe czerwienie"),
    ("Kodak_Kodachrome_200.xmp",       "Kodak",  "Kodachrome 200",      200,  "slide", "Nasycone czerwienie i zielenie, jasne światła"),
    ("Kodak_Kodachrome_Original.xmp",  "Kodak",  "Kodachrome Original", 0,    "slide", "Bardzo miękki kontrast, żywe, łagodne barwy"),
    ("Ilford_Delta_100.xmp",           "Ilford", "Delta 100",           100,  "bw",    "Czysty, drobnoziarnisty, szeroka rozpiętość tonów"),
    ("Ilford_Delta_400.xmp",           "Ilford", "Delta 400",           400,  "bw",    "Uniwersalny, wyraźny kontrast, średnie ziarno"),
    ("Ilford_Delta_3200.xmp",          "Ilford", "Delta 3200",          3200, "bw",    "Miękki kontrast, rozjaśnione cienie"),
    ("Ilford_FP4.xmp",                 "Ilford", "FP4",                 125,  "bw",    "Klasyczny, łagodne światła, drobne ziarno"),
    ("Ilford_HP5.xmp",                 "Ilford", "HP5",                 400,  "bw",    "Reportażowy, miękkie światła, wyraźne ziarno"),
    ("Ilford_HP5__ver._2_.xmp",        "Ilford", "HP5 (ver. 2)",        400,  "bw",    "Mocny kontrast, głęboka czerń, wyraźne ziarno"),
    ("Fuji_Superia_100_.xmp",          "Fuji",   "Superia 100",         100,  "neg",   "Jasne, świeże zielenie i żółcie, czyste kolory"),
    ("Fuji_Superia_400.xmp",           "Fuji",   "Superia 400",         400,  "neg",   "Podniesiona czerń, stonowane zielenie"),
    ("Fuji_Superia_800.xmp",           "Fuji",   "Superia 800",         800,  "neg",   "Wyblakła czerń, chłodne zielenie, wyraźne ziarno"),
    ("Fuji_Superia_1600.xmp",          "Fuji",   "Superia 1600",        1600, "neg",   "Mocno podniesiona czerń, grube ziarno"),
    ("Fuji_Pro_S.xmp",                 "Fuji",   "Pro S",               160,  "neg",   "Delikatne, neutralne barwy, jasna skóra i zieleń"),
]
KIND = {"neg": "Film kolorowy negatywowy", "slide": "Film kolorowy slajdowy", "bw": "Film czarno-biały negatywowy"}

# --- attributes carried over, with how the module treats them
#  S = scaled by "Efekt", G = signed (printed with +), T = technical (only when Efekt > 0), 10 = stored x10,
#  C = also gets the "Kontrast" slider, A = grain amount (also scaled by the "Ziarno" slider)
COL = ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]
ATTRS = [("Contrast2012", "SGC"), ("Highlights2012", "SG"), ("Shadows2012", "SG"), ("Whites2012", "SG"),
         ("Blacks2012", "SG"), ("Clarity2012", "SG"), ("Dehaze", "SG"), ("Vibrance", "SG"), ("Saturation", "SG"),
         ("ParametricShadows", "SG"), ("ParametricDarks", "SG"), ("ParametricLights", "SG"), ("ParametricHighlights", "SG"),
         ("ParametricShadowSplit", ""), ("ParametricMidtoneSplit", ""), ("ParametricHighlightSplit", "")]
ATTRS += [("HueAdjustment" + c, "SG") for c in COL]
ATTRS += [("SaturationAdjustment" + c, "SG") for c in COL]
ATTRS += [("LuminanceAdjustment" + c, "SG") for c in COL]
ATTRS += [("GrayMixer" + c, "SG") for c in COL]
ATTRS += [("RedHue", "SG"), ("RedSaturation", "SG"), ("GreenHue", "SG"), ("GreenSaturation", "SG"),
          ("BlueHue", "SG"), ("BlueSaturation", "SG"), ("ShadowTint", "SG")]
ATTRS += [("GrainAmount", "SA"), ("GrainSize", ""), ("GrainFrequency", "")]
ATTRS += [("Sharpness", "T"), ("SharpenRadius", "T10"), ("SharpenDetail", "T"), ("SharpenEdgeMasking", "T"),
          ("LuminanceSmoothing", "T"), ("LuminanceNoiseReductionDetail", "T"), ("LuminanceNoiseReductionContrast", "T"),
          ("ColorNoiseReduction", "T"), ("ColorNoiseReductionDetail", "T"), ("ColorNoiseReductionSmoothness", "T")]
CURVES = ["ToneCurvePV2012", "ToneCurvePV2012Red", "ToneCurvePV2012Green", "ToneCurvePV2012Blue"]
MAXPTS = 8
PROFILE = {"Adobe Standard": 1, "Camera Standard": 2}      # "Embedded" (JPEG / DNG only) is left out


def num(s, tenths=False):
    f = float(s)
    return int(round(f * 10)) if tenths else int(round(f))


def parse(path):
    t = open(path, encoding="utf-8").read()
    a = dict(re.findall(r'crs:(\w+)="([^"]*)"', t))
    curves = {}
    for m in re.finditer(r'<crs:(ToneCurvePV2012\w*)>\s*<rdf:Seq>(.*?)</rdf:Seq>', t, re.S):
        pts = [tuple(int(v) for v in x.split(",")) for x in re.findall(r'<rdf:li>([^<]*)</rdf:li>', m.group(2))]
        curves[m.group(1)] = pts
    return a, curves


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(src, out):
    films = []
    for fname, brand, name, iso, typ, desc in META:
        if len(desc) > 52:
            raise SystemExit("description too long (%d): %s" % (len(desc), desc))
        a, curves = parse(os.path.join(src, fname))
        vals = []
        for i, (n, fl) in enumerate(ATTRS):
            if n in a:
                vals.append((i, num(a[n], "10" in fl)))
        mono = a.get("ConvertToGrayscale") == "True"
        if mono != (typ == "bw"):
            raise SystemExit("%s: B&W flag in preset (%s) disagrees with META" % (fname, mono))
        cv = []
        for c in CURVES:
            pts = curves.get(c, [(0, 0), (255, 255)])
            if len(pts) > MAXPTS:
                raise SystemExit("%s: curve %s has %d points" % (fname, c, len(pts)))
            cv.append(pts)
        films.append(dict(file=fname, brand=BRANDS.index(brand), name=name, full=brand + " " + name, iso=iso,
                          kind=KIND[typ], desc=desc, mono=mono,
                          wb_custom=1 if a.get("WhiteBalance") == "Custom" else 0,
                          temp=num(a.get("Temperature", "0")), tint=num(a.get("Tint", "0")),
                          profile=PROFILE.get(a.get("CameraProfile", ""), 0), vals=vals, cv=cv))
    films.sort(key=lambda f: f["brand"])      # stable: keeps the META order inside a brand

    o = []
    w = o.append
    w("/* films.h - GENERATED by tools/preset2films.py from the Lightroom presets. Do not edit by hand.\n"
      " * Every film carries the complete preset (sliders, HSL / B&W mixer, tone curves, calibration, grain, ...). */\n")
    w("#ifndef FL_FILMS_H\n#define FL_FILMS_H\n\n")
    w("#define F_SCALE   1     /* scaled by the Efekt slider */\n#define F_SIGN    2     /* printed with + / - */\n"
      "#define F_TECH    4     /* sharpening / noise reduction: only written when Efekt > 0, never scaled */\n"
      "#define F_TENTHS  8     /* stored x10 (SharpenRadius) */\n#define F_CON     16    /* gets the Kontrast slider */\n"
      "#define F_GRAIN   32    /* grain amount: also scaled by the Ziarno slider */\n\n")
    w("typedef struct { const char *name; unsigned char flags; } attr_t;\n")
    w("static const attr_t attr_defs[] = {\n")
    for n, fl in ATTRS:
        f = 0
        if "S" in fl: f |= 1
        if "G" in fl: f |= 2
        if "T" in fl: f |= 4
        if "10" in fl: f |= 8
        if "C" in fl: f |= 16
        if "A" in fl: f |= 32
        w('    { "%s", %d },\n' % (n, f))
    w("};\n#define ATTR_COUNT ((int)(sizeof(attr_defs) / sizeof(attr_defs[0])))\n\n")
    w("typedef struct { unsigned char id; short v; } fv_t;\n\n")
    w("#define CURVE_MAX %d\n" % MAXPTS)
    w("typedef struct {\n    unsigned char brand;      /* index into brands[] */\n"
      "    const char *name;         /* shown under the brand (UTF-8, Polish letters are converted for the font) */\n"
      "    const char *full;         /* brand + name: goes into the XMP and the keyword */\n"
      "    const char *kind;         /* colour / B&W, negative / slide */\n"
      "    const char *desc;         /* short description */\n"
      "    short iso;                /* 0 = not shown */\n"
      "    unsigned char mono, wb_custom, profile;   /* profile: 0 none, 1 Adobe Standard, 2 Camera Standard */\n"
      "    short temp, tint;\n    unsigned char nv;\n    const fv_t *v;\n"
      "    unsigned char nc[4];      /* points of master, red, green, blue tone curve */\n"
      "    unsigned char cv[4][CURVE_MAX][2];\n} film_t;\n\n")
    w("static const char *const brands[] = { %s };\n#define BRAND_COUNT ((int)(sizeof(brands) / sizeof(brands[0])))\n\n"
      % ", ".join(cstr(b) for b in BRANDS))
    for i, f in enumerate(films):
        w("/* %s */\nstatic const fv_t fv_%d[] = {" % (f["file"], i))
        for k, (idx, v) in enumerate(f["vals"]):
            if k % 6 == 0: w("\n    ")
            w("{%d,%d}, " % (idx, v))
        w("\n};\n")
    w("\nstatic const film_t films[] = {\n")
    for i, f in enumerate(films):
        w("    { %d, %s, %s,\n      %s,\n      %s, %d,\n      %d, %d, %d, %d, %d,\n      %d, fv_%d,\n      { %s },\n      {\n"
          % (f["brand"], cstr(f["name"]), cstr(f["full"]), cstr(f["kind"]), cstr(f["desc"]), f["iso"],
             1 if f["mono"] else 0, f["wb_custom"], f["profile"], f["temp"], f["tint"],
             len(f["vals"]), i, ", ".join(str(len(c)) for c in f["cv"])))
        for c in f["cv"]:
            pts = list(c) + [(0, 0)] * (MAXPTS - len(c))
            w("        { %s },\n" % ", ".join("{%d,%d}" % p for p in pts))
        w("      } },\n")
    w("};\n\n#define FILM_COUNT ((int)(sizeof(films) / sizeof(films[0])))\n\n#endif\n")
    open(out, "w", encoding="utf-8").write("".join(o))
    print("films:", len(films), " attrs:", len(ATTRS))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
