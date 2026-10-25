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
        if not fs: raise SystemExit("calibration: no film called " + full)
        for n, v in adj.items(): _set(fs[0], INDEX[n], v)
    return report, missing
