# -*- coding: utf-8 -*-
"""Calibration of the films against the manufacturers' figures (see grain_targets.py), applied by preset2films.py after
the presets are read.  Everything that is not listed here stays exactly as in the preset.

  GRAIN   GrainAmount from the grain figure of the film (variants keep their ratio to the base film)
  ADJ     other values that were corrected after measuring the rendered pictures (tools/calib_report.py):
          full name -> {attribute: value}; the value replaces the preset's one
  OWN     the empty "Własny preset" slots get a base grain, so that the Ziarno slider has something to scale
"""
import grain_targets as G

ADJ = {
    "Kodak Ektachrome 320T":           {"Contrast2012": 130, "Saturation": -20, "Exposure2012": 23, "Blacks2012": 8},
    "Kodak Ektar 100":                 {"Contrast2012": 53, "Saturation": 16, "Exposure2012": 15, "Blacks2012": -19},
    "Kodak Gold 200":                  {"Contrast2012": 40, "Saturation": 4, "Exposure2012": 15, "Blacks2012": 20},
    "Kodak Portra 160":                {"Contrast2012": -15, "Saturation": 4, "Exposure2012": 50, "Blacks2012": 20},
    "Kodak Portra 160NC":              {"Contrast2012": -20, "Saturation": 0, "Exposure2012": 40, "Blacks2012": 25},
    "Kodak Portra 400":                {"Contrast2012": -8, "Saturation": 17, "Exposure2012": 50, "Blacks2012": 12},
    "Kodak Portra 400NC":              {"Contrast2012": -12, "Saturation": -5, "Exposure2012": 45, "Blacks2012": 15},
    "Kodak Portra 800":                {"Contrast2012": -15, "Saturation": 22, "Exposure2012": 20, "Blacks2012": -18},
    "Kodak Ektachrome 64":             {"Contrast2012": -10, "Saturation": -1, "Exposure2012": 18, "Blacks2012": -28},
    "Kodak Ektachrome 100G":           {"Contrast2012": -80, "Saturation": 4, "Exposure2012": 23, "Blacks2012": -25},
    "Kodak Ektachrome 100VS":          {"Contrast2012": -15, "Saturation": 56, "Exposure2012": 41, "Blacks2012": 9},
    "Kodak Ektachrome 200":            {"Contrast2012": -24, "Saturation": 7, "Exposure2012": 13, "Blacks2012": -28},
    "Kodak Ektachrome 400X":           {"Contrast2012": -16, "Saturation": 11, "Exposure2012": 29, "Blacks2012": -29},
    "Kodak Ektachrome E100":           {"Contrast2012": -39, "Saturation": 3, "Exposure2012": 9, "Blacks2012": -30},
    "Kodak Elite Chrome 200":          {"Contrast2012": -34, "Saturation": 17, "Exposure2012": 4, "Blacks2012": -20},
    "Kodak Elite Chrome 400":          {"Contrast2012": -72, "Saturation": 19, "Exposure2012": 27, "Blacks2012": -25},
    "Kodak Kodachrome 25":             {"Contrast2012": 7, "Saturation": 1, "Exposure2012": 7, "Blacks2012": -10},
    "Kodak Kodachrome 64":             {"Contrast2012": 5, "Saturation": -3, "Exposure2012": 11, "Blacks2012": -19},
    "Kodak Kodachrome 200":            {"Contrast2012": -7, "Saturation": 3, "Exposure2012": 7, "Blacks2012": -25},
    "Fuji Astia 100F":                 {"Contrast2012": -56, "Saturation": 4, "Exposure2012": -2, "Blacks2012": -8},
    "Fuji Fortia SP 50":               {"Contrast2012": -35, "Saturation": 30, "Exposure2012": 70, "Blacks2012": 24},
    "Fuji Provia 100F":                {"Contrast2012": -71, "Saturation": 6, "Exposure2012": 40, "Blacks2012": -25},
    "Fuji Provia 400F":                {"Contrast2012": -31, "Saturation": 8, "Exposure2012": 23, "Blacks2012": -28},
    "Fuji Provia 400X":                {"Contrast2012": -77, "Saturation": 6, "Exposure2012": 39, "Blacks2012": -28},
    "Fuji Sensia 100":                 {"Contrast2012": -77, "Saturation": 10, "Exposure2012": 27, "Blacks2012": -22},
    "Fuji Velvia 50":                  {"Contrast2012": 0, "Saturation": 38, "Exposure2012": 51, "Blacks2012": 17},
    "Fuji Velvia 100":                 {"Contrast2012": -24, "Saturation": 27, "Exposure2012": 40, "Blacks2012": -23},
    "AgfaChrome RS 100":               {"Contrast2012": -60, "Saturation": 5, "Exposure2012": 17, "Blacks2012": -20},
}

# v0.10 "wow": the looks of the short list are pushed so that each one has a clear character of its own (the presets are made
# for a raw converter and are often shy). ADD = added to the value the preset has, SET = replaces it. Colour grading hues are
# degrees on the wheel (0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta). Everything is visible in the contact
# sheets of tools/ (see the project notes), the numbers were chosen by looking at the rendered pictures.
WOW_ADD = {
    "Kodak Portra 400":       {"Contrast2012": -2, "Blacks2012": 10, "Vibrance": 16, "Saturation": 6, "SaturationAdjustmentOrange": 10,
                               "LuminanceAdjustmentOrange": 10, "SaturationAdjustmentGreen": 10, "SaturationAdjustmentYellow": 10},
    "Kodak Ektar 100":        {"Saturation": 16, "Vibrance": 12, "Contrast2012": 20, "Blacks2012": -10, "SaturationAdjustmentRed": 10,
                               "SaturationAdjustmentOrange": -10, "SaturationAdjustmentBlue": 10, "LuminanceAdjustmentBlue": -8},
    "Kodak Gold 200":         {"Saturation": 18, "SaturationAdjustmentYellow": 15, "PostCropVignetteAmount": -12},
    "Fuji Superia X-TRA 400": {"Saturation": 12, "SaturationAdjustmentGreen": 30, "Contrast2012": 14},
    "Kodak Vision3 500T":     {"Contrast2012": 24, "Blacks2012": 6, "Saturation": -18},
    "Kodachrome 64":          {"Saturation": 4, "Contrast2012": 20, "SaturationAdjustmentRed": 20, "SaturationAdjustmentYellow": 10,
                               "SaturationAdjustmentBlue": -8},
    "Fuji Astia 100F":        {"Contrast2012": 24, "Vibrance": 6},
    "Kodak Ektachrome 100VS": {"Saturation": -14, "Contrast2012": 15, "SaturationAdjustmentAqua": 20, "SaturationAdjustmentBlue": 15},
    "Fuji Velvia 50":         {"Saturation": -6, "SaturationAdjustmentOrange": -8},
    "Kodak Tri-X 400":        {"Contrast2012": 20, "Blacks2012": -10, "Whites2012": 8},
    "Ilford HP5 Plus":        {"Contrast2012": 8},
    "Ilford XP2 Super":       {"Contrast2012": 10, "GrainAmount": -8},
    "Ilford Delta 3200":      {"Blacks2012": 10, "Contrast2012": 12, "GrainAmount": 10},
}
WOW_SET = {
    "Kodak Portra 400":       {"ColorGradeHighlightHue": 38, "ColorGradeHighlightSat": 44, "ColorGradeShadowHue": 195, "ColorGradeShadowSat": 22,
                               "ColorGradeMidtoneHue": 25, "ColorGradeMidtoneSat": 12},
    "Kodak Ektar 100":        {"ColorGradeShadowHue": 215, "ColorGradeShadowSat": 14, "ColorGradeHighlightHue": 48, "ColorGradeHighlightSat": 14},
    "Kodak Gold 200":         {"ColorGradeShadowHue": 25, "ColorGradeShadowSat": 22, "ColorGradeMidtoneHue": 40, "ColorGradeMidtoneSat": 16,
                               "ColorGradeHighlightHue": 48, "ColorGradeHighlightSat": 36},
    "Fuji Superia X-TRA 400": {"ColorGradeShadowHue": 150, "ColorGradeShadowSat": 24, "ColorGradeMidtoneHue": 140, "ColorGradeMidtoneSat": 8,
                               "ColorGradeHighlightHue": 335, "ColorGradeHighlightSat": 14},
    "Kodak Vision3 500T":     {"ColorGradeShadowHue": 190, "ColorGradeShadowSat": 60, "ColorGradeMidtoneHue": 35, "ColorGradeMidtoneSat": 10,
                               "ColorGradeHighlightHue": 28, "ColorGradeHighlightSat": 70},
    "Kodachrome 64":          {"ColorGradeShadowHue": 12, "ColorGradeShadowSat": 28, "ColorGradeHighlightHue": 42, "ColorGradeHighlightSat": 40},
    "Fuji Astia 100F":        {"ColorGradeHighlightHue": 25, "ColorGradeHighlightSat": 14},
    "Kodak Ektachrome 100VS": {"ColorGradeShadowHue": 225, "ColorGradeShadowSat": 40, "ColorGradeHighlightHue": 195, "ColorGradeHighlightSat": 26},
    "Ilford XP2 Super":       {"ColorGradeHighlightHue": 40, "ColorGradeHighlightSat": 8},
}

# films that are made from another film of the list: everything of the base film (sliders, HSL, curves, white balance) is
# copied after the base film's own corrections, then the differences are added; grain stays the film's own (GRAIN above).
# "Kodak Royal Gold 400" = Gold 200 with richer colour and deeper blacks (the 400 was the more saturated emulsion).
DERIVE = {
    "Kodak Royal Gold 400": ("Kodak Gold 200", {"Contrast2012": 8, "Saturation": 10, "Vibrance": 5, "Blacks2012": -6}),
}
KEEP_OWN = ("GrainAmount", "GrainSize", "GrainFrequency")

OWN = {"GrainAmount": 25, "GrainSize": 30, "GrainFrequency": 50}


def _set(f, idx, v):
    for k, (i, _) in enumerate(f["vals"]):
        if i == idx:
            f["vals"][k] = (i, v); return
    f["vals"].append((idx, v))
    f["vals"].sort()


def _get(f, idx, default=0):
    for i, v in f["vals"]:
        if i == idx: return v
    return default


def apply(allf, INDEX):
    ga = INDEX["GrainAmount"]
    base = {f["full"]: f for f in allf if f["shift"] == 0}
    report, missing = [], []
    for f in allf:
        if f["full"].startswith("Własny preset"):
            for n, v in OWN.items(): _set(f, INDEX[n], v)
            continue
        if f["shift"] == 0:
            b = f
        else:
            b = base.get(f["full"].rsplit(" ", 1)[0])
        if b is None or b["full"] not in G.T:
            if f["shift"] == 0: missing.append(f["full"])
            continue
        want = max(0, min(100, int(round(G.T[b["full"]][0] * G.GAIN))))
        if f["shift"] == 0:
            old = _get(f, ga)
            f["_grain_old"] = old
            _set(f, ga, want)
            report.append((f["full"], old, want))
        else:
            ob = b.get("_grain_old") or 1
            _set(f, ga, max(0, min(100, int(round(_get(f, ga) * want / ob)))))
    for full, adj in ADJ.items():
        fs = [f for f in allf if f["full"] == full]
        if not fs: continue                                # the film is not in the (short) list
        for n, v in adj.items(): _set(fs[0], INDEX[n], v)
    for full, adj in WOW_SET.items():
        fs = [f for f in allf if f["full"] == full]
        if fs:
            for n, v in adj.items(): _set(fs[0], INDEX[n], v)
    for full, adj in WOW_ADD.items():
        fs = [f for f in allf if f["full"] == full]
        if fs:
            for n, v in adj.items(): _set(fs[0], INDEX[n], _get(fs[0], INDEX[n]) + v)
    for child, (parent, delta) in DERIVE.items():
        c = [f for f in allf if f["full"] == child]
        p = [f for f in allf if f["full"] == parent]
        if not c or not p: continue
        c, p = c[0], p[0]
        own = {n: _get(c, INDEX[n]) for n in KEEP_OWN}
        c["vals"] = list(p["vals"])
        c["cv"] = [list(x) for x in p["cv"]]
        for k in ("wb_custom", "temp", "tint", "profile"): c[k] = p[k]
        for n, v in own.items(): _set(c, INDEX[n], v)
        for n, dv in delta.items(): _set(c, INDEX[n], _get(c, INDEX[n]) + dv)
    return report, missing
