# -*- coding: utf-8 -*-
"""Calibration of the films against the manufacturers' figures (see grain_targets.py), applied by preset2films.py after
the presets are read.  Everything that is not listed here stays exactly as in the preset.

  GRAIN   GrainAmount from the grain figure of the film (variants keep their ratio to the base film)
  ADJ     other values that were corrected after measuring the rendered pictures (tools/calib_report.py):
          full name -> {attribute: value}; the value replaces the preset's one
  OWN     the empty "Własny look" gets a base grain, so that the Ziarno slider has something to scale
"""
import grain_targets as G

ADJ = {}

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
        if f["full"] == "Własny look":
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
