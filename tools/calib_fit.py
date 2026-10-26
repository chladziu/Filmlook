#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""calib_fit.py <work dir> <targets.json> <out.json>

Finds, for every film in targets.json, the values of Contrast2012 / Saturation / Exposure2012 / Blacks2012 for which the
JPG engine (tools_host/hostdev) gives the wanted look on three real photos:
   hsat  saturation of the result / saturation of the camera's picture (HSV, 1.00 = unchanged)
   lstd  contrast: standard deviation of the lightness, result / original
   dL    mean lightness shift (L*)
   p2    2nd percentile of L* (black point): p2 = exact target, p2min = Blacks is raised until it is at least this
The fitted values go to calibration.ADJ (tools/calibration.py).  Needs numpy, scikit-image and Pillow.
work dir: contains hostdev, dump2 (prints the values of a film) and cr2/IMG_*_prev.jpg + cr2/IMG_*.CR2
"""
import json, os, subprocess, sys
import numpy as np
from PIL import Image
from skimage.color import rgb2lab, rgb2hsv
from multiprocessing import Pool

W = sys.argv[1]
PH = ["IMG_8581", "IMG_8614", "IMG_8657"]


def load(p):
    im = Image.open(p); im.draft("RGB", (702, 468)); im = im.convert("RGB")
    return np.asarray(im, dtype=np.float32)[::2, ::2] / 255


ORIG = {k: load("%s/cr2/%s_prev.jpg" % (W, k)) for k in PH}
OL = {k: rgb2lab(v) for k, v in ORIG.items()}
OH = {k: rgb2hsv(v) for k, v in ORIG.items()}


def base_vals(fid):
    out = subprocess.run(["%s/dump2" % W, str(fid)], capture_output=True, text=True).stdout.split("\n")[1]
    d = {}
    for tok in out.split("  "):
        if "=" in tok:
            k, v = tok.strip().split("="); d[k] = int(v)
    return d


def measure(fid, sets):
    hs, ls, dl, p2 = [], [], [], []
    for k in PH:
        p = "/tmp/fit_%d_%d.jpg" % (os.getpid(), fid)
        args = ["%s/hostdev" % W, "%s/cr2/%s.CR2" % (W, k), p, str(fid), "2", "grain=0"] + ["set=%s:%d" % kv for kv in sets.items()]
        subprocess.run(args, capture_output=True)
        o = load(p); os.remove(p)
        lab = rgb2lab(o); L0 = OL[k]; mv = OH[k][..., 2] > 0.12
        hs.append(rgb2hsv(o)[..., 1][mv].mean() / max(OH[k][..., 1][mv].mean(), 1e-6))
        ls.append(lab[..., 0].std() / L0[..., 0].std()); dl.append(lab[..., 0].mean() - L0[..., 0].mean())
        p2.append(np.percentile(lab[..., 0], 2))
    return dict(hsat=float(np.mean(hs)), lstd=float(np.mean(ls)), dL=float(np.mean(dl)), p2=float(np.mean(p2)))


def secant(f, x0, x1, target, lo, hi, n=2):
    y0, y1 = f(x0), f(x1)
    x = x1
    for _ in range(n):
        if abs(y1 - y0) < 1e-9: break
        x = int(round(x1 + (target - y1) * (x1 - x0) / (y1 - y0)))
        x = max(lo, min(hi, x))
        if x == x1: break
        x0, y0, x1, y1 = x1, y1, x, f(x)
        if abs(y1 - target) < 0.02: break
    return x1 if abs(y1 - target) <= abs(y0 - target) else x0


def fit(job):
    fid, tg = job
    b = base_vals(fid)
    cur = dict(Contrast2012=b.get("Contrast2012", 0), Saturation=b.get("Saturation", 0),
               Exposure2012=b.get("Exposure2012", 0), Blacks2012=b.get("Blacks2012", 0))
    mono = "hsat" not in tg
    for rnd in range(3):
        if "lstd" in tg:
            f = lambda c: measure(fid, dict(cur, Contrast2012=c))["lstd"]
            cur["Contrast2012"] = secant(f, cur["Contrast2012"], cur["Contrast2012"] + 15, tg["lstd"], -100, 100)
        if "hsat" in tg:
            f = lambda s: measure(fid, dict(cur, Saturation=s))["hsat"]
            cur["Saturation"] = secant(f, cur["Saturation"], cur["Saturation"] + 15, tg["hsat"], -100, 100)
        if "dL" in tg:
            f = lambda e: measure(fid, dict(cur, Exposure2012=e))["dL"]
            cur["Exposure2012"] = secant(f, cur["Exposure2012"], cur["Exposure2012"] + 30, tg["dL"], -150, 150)
        if "p2" in tg or "p2min" in tg:
            m = measure(fid, cur)
            want = tg.get("p2", tg.get("p2min"))
            if ("p2" in tg and abs(m["p2"] - want) > 1.5) or ("p2min" in tg and m["p2"] < want):
                f = lambda bl: measure(fid, dict(cur, Blacks2012=bl))["p2"]
                cur["Blacks2012"] = secant(f, cur["Blacks2012"], cur["Blacks2012"] + 15, want, -100, 100)
    m = measure(fid, cur)
    return dict(id=fid, vals=cur, got=m, want=tg)


if __name__ == "__main__":
    targets = json.load(open(sys.argv[2]))
    jobs = [(int(k), v) for k, v in targets.items()]
    with Pool(2) as P: res = P.map(fit, jobs)
    json.dump(res, open(sys.argv[3], "w"), indent=1)
    print("fitted", len(res))
