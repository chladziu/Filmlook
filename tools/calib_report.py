#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""calib_report.py <measure.json> [films.index.json]

Prints what the JPG engine really does with every film (measured on three real photos, a grey ramp and a flat grey
patch, see calib_measure.py): saturation, contrast, black / white point, brightness shift, grain, warmth.
Columns
  sat     saturation of the result / saturation of the photo as the camera made it (HSV, 1.00 = unchanged)
  con     standard deviation of the lightness, result / original (1.00 = unchanged)
  black   2nd percentile of L* (0 = pure black), white = 98th percentile
  dL      mean lightness shift in L* (negative = darker)
  grain   standard deviation of a flat mid-grey patch (levels of 255), Ziarno 100
  warm    shift of CIELAB b* of the colours (positive = more yellow); mono films show 0
"""
import json, sys

def main(path, idx_path):
    res = json.load(open(path))
    idx = json.load(open(idx_path))
    print("%3s %-34s %5s %5s %5s %5s %6s %6s %6s" % ("id", "film", "sat", "con", "black", "white", "dL", "grain", "warm"))
    for r in res:
        f = idx[r["id"]]
        mono = f["mono"]
        print("%3d %-34s %5.2f %5.2f %5.1f %5.1f %+6.1f %6.2f %+6.1f" % (
            r["id"], f["full"][:34], 0.0 if mono else r["hsat"], r["lstd"], r["p2"], r["p98"], r["dL"], r["gsig"],
            0.0 if mono else r["db"]))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "films.index.json")
