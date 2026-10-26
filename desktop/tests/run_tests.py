#!/usr/bin/env python3
"""The tests of FILM LOOK DESKTOP: does it make exactly the same picture as the module's own PC tool (hostdev), and does it make the
same picture on every computer?

  1. hostdev (modules/filmlook/tools_host/hostdev.c, built here from the module's sources)  ==  the command line version, byte for byte,
     for several films, sliders and (engine) sizes, on every photo of the samples folder
  2. the browser version (the .wasm run under Node)  ==  the command line version, byte for byte (when --wasm is given)
  3. the sizes M and S: the right dimensions, and the same bytes in the command line version and in the browser version
  4. a file with the SHA-256 of every result: the same file must come out on Windows, macOS and Linux (compared by the workflow)

usage: run_tests.py --cli filmlook_cli --samples DIR [--wasm fd_engine.wasm --node node] [--hashes hashes.txt] [--cc cc]
The samples folder holds *.jpg / *.jpeg / *.cr2 (tests/make_samples.py makes some, real camera files are better)."""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MODULE = os.path.join(ROOT, "modules", "filmlook")

ap = argparse.ArgumentParser()
ap.add_argument("--cli", required=True)
ap.add_argument("--samples", required=True)
ap.add_argument("--wasm")
ap.add_argument("--node", default="node")
ap.add_argument("--hashes")
ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
ap.add_argument("--no-hostdev", action="store_true", help="do not compare with hostdev")
args = ap.parse_args()

fails = []
hashes = []
work = tempfile.mkdtemp(prefix="fdtest_")


def check(ok, msg):
    print(("ok    " if ok else "FAIL  ") + msg, flush=True)
    if not ok:
        fails.append(msg)


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def jpeg_size(data):
    """(width, height) from the first SOF marker of a JPEG"""
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        m = data[i + 1]
        if m in (0xC0, 0xC1, 0xC2):
            return (data[i + 7] << 8) | data[i + 8], (data[i + 5] << 8) | data[i + 6]
        i += 2 + ((data[i + 2] << 8) | data[i + 3])
    return None


def source_size(path):
    """size of the picture in a JPG, or of the preview in a CR2 (IFD0 holds its width and height)"""
    data = open(path, "rb").read(70000)
    if data[:2] == b"\xff\xd8":
        return jpeg_size(data)
    ifd = int.from_bytes(data[4:8], "little")
    n = int.from_bytes(data[ifd:ifd + 2], "little")
    tags = {}
    for k in range(n):
        e = ifd + 2 + 12 * k
        tags[int.from_bytes(data[e:e + 2], "little")] = int.from_bytes(data[e + 8:e + 12], "little") & 0xFFFF if int.from_bytes(data[e + 2:e + 4], "little") == 3 else int.from_bytes(data[e + 8:e + 12], "little")
    return tags[0x100], tags[0x101]


def expected_size(sw, sh, target_long):
    ls, ss = max(sw, sh), min(sw, sh)
    if target_long >= ls:
        return sw, sh
    st = (ss * target_long + ls // 2) // ls
    return (target_long, st) if sw >= sh else (st, target_long)


# ------------------------------------------------------------------ hostdev, built from the module's own files
hostdev = None
if not args.no_hostdev:
    exe = os.path.join(work, "hostdev" + (".exe" if os.name == "nt" else ""))
    r = run([args.cc, "-O2", "-std=gnu99", "-DSE_HOSTED=1", "-I", MODULE, "-o", exe,
             os.path.join(MODULE, "tools_host", "hostdev.c"), os.path.join(MODULE, "engine_unit.c")])
    if r.returncode != 0:
        print(r.stderr.decode(errors="replace"))
        check(False, "hostdev builds")
    else:
        hostdev = exe

photos = sorted(f for f in os.listdir(args.samples) if f.lower().endswith((".jpg", ".jpeg", ".cr2")))
if not photos:
    sys.exit("no photos in " + args.samples)

# the cases: (film index 0 based, sliders as key=value for hostdev, the same for the command line version)
CASES = [
    (0, {}),
    (6, dict(grain=150, contrast=10, fade=20, warm=-30, shadow=20, high=-15, sat=15, vig=40)),
    (10, dict(strength=60, grain=0)),
    (13, dict(vig=70, sat=-20)),
]
KEYMAP = {"strength": "--efekt", "grain": "--ziarno", "contrast": "--kontrast", "fade": "--matowosc", "warm": "--cieplo",
          "shadow": "--cienie", "high": "--swiatla", "sat": "--nasycenie", "vig": "--winieta"}
ORDER = ["strength", "grain", "contrast", "fade", "warm", "shadow", "high", "sat", "vig"]


def cli_args(film, sl):
    a = ["--film", str(film + 1)]
    for k, v in sl.items():
        a += [KEYMAP[k], str(v)]
    return a


def cli_make(photo, film, sl, extra, outname):
    d = os.path.join(work, "cli_" + outname)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    r = run([args.cli, "--quiet", "--quality", "95"] + cli_args(film, sl) + extra + ["--out", d, photo])
    out = os.listdir(d)
    if r.returncode != 0 or len(out) != 1:
        print(r.stderr.decode(errors="replace"))
        return None
    return os.path.join(d, out[0])


def wasm_make(photo, film, size, sl, outpath):
    s = {"strength": 100, "grain": 100}
    s.update(sl)
    a = [args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, photo, outpath, str(film), str(size), "95"]
    a += [str(s.get(k, 0)) for k in ORDER]
    r = run(a)
    if r.returncode != 0:
        print(r.stderr.decode(errors="replace"))
        return None
    return outpath


for photo in photos:
    path = os.path.join(args.samples, photo)
    for film, sl in CASES:
        for eng_scale in (1, 2):
            tag = "%s film %d scale %d" % (photo, film + 1, eng_scale)
            got = cli_make(path, film, sl, ["--scale", str(eng_scale)], "a")
            if not got:
                check(False, tag + ": command line version ran")
                continue
            hashes.append("%s  %s" % (sha(got), tag.replace(" ", "_")))
            if hostdev:
                ref = os.path.join(work, "ref.jpg")
                hd = [hostdev, path, ref, str(film), str(eng_scale), "quality=95"] + ["%s=%d" % kv for kv in sl.items()]
                r = run(hd)
                if r.returncode != 0:
                    print(r.stderr.decode(errors="replace"))
                    check(False, tag + ": hostdev ran")
                else:
                    check(sha(ref) == sha(got), tag + ": command line == hostdev")
            if args.wasm:
                w = wasm_make(path, film, 100 + eng_scale, sl, os.path.join(work, "w.jpg"))
                check(bool(w) and sha(w) == sha(got), tag + ": browser version == command line")

# sizes M and S (and the full size with the size option)
for photo in photos:
    path = os.path.join(args.samples, photo)
    for size_name, size_code in (("L", 0), ("M", 1), ("S", 2)):
        film, sl = CASES[1]
        got = cli_make(path, film, sl, ["--size", size_name], "s")
        tag = "%s size %s" % (photo, size_name)
        if not got:
            check(False, tag + ": command line version ran")
            continue
        hashes.append("%s  %s" % (sha(got), tag.replace(" ", "_")))
        with open(got, "rb") as f:
            have = jpeg_size(f.read(70000))
        sw, sh = source_size(path)
        want = (sw, sh) if size_name == "L" else expected_size(sw, sh, 3861 if size_name == "M" else 2784)
        check(have == want, tag + ": size %s (wanted %s)" % (have, want))
        if args.wasm:
            w = wasm_make(path, film, size_code, sl, os.path.join(work, "w.jpg"))
            check(bool(w) and sha(w) == sha(got), tag + ": browser version == command line")
        if size_name == "L" and hostdev:
            ref = os.path.join(work, "ref.jpg")
            run([hostdev, path, ref, str(film), "1", "quality=95"] + ["%s=%d" % kv for kv in sl.items()])
            check(sha(ref) == sha(got), tag + ": size L == hostdev")

# a folder and a file with Polish letters in the name (the command line version must open and write them on every system)
uni_dir = os.path.join(work, "zażółć gęślą jaźń")
os.makedirs(uni_dir, exist_ok=True)
uni_src = os.path.join(uni_dir, "zdjęcie_żółć.jpg")
with open(os.path.join(args.samples, photos[0]), "rb") as fi, open(uni_src, "wb") as fo:
    fo.write(fi.read())
r = run([args.cli, "--quiet", "--film", "1", "--size", "S", uni_dir])
made = os.path.join(uni_dir, "FILMLOOK")
check(r.returncode == 0 and os.path.isdir(made) and len(os.listdir(made)) >= 1, "folder and file names with Polish letters: " + str(os.listdir(made) if os.path.isdir(made) else r.stderr.decode(errors="replace")[:200]))

# the resizer against an independent exact area average (needs numpy and pillow; skipped when they are not there)
try:
    import numpy as np
    from PIL import Image

    def area(img, n_out, axis):
        n_in = img.shape[axis]
        img = np.moveaxis(img, axis, 0).astype(np.float64)
        cs = np.concatenate([np.zeros((1,) + img.shape[1:]), np.cumsum(img, axis=0)], axis=0)
        a = np.arange(n_out) * n_in / n_out
        b = (np.arange(n_out) + 1) * n_in / n_out

        def S(t):
            i = np.minimum(np.floor(t).astype(int), n_in - 1)
            return cs[i] + (t - i)[:, None, None] * img[i]
        return np.moveaxis((S(b) - S(a)) / (n_in / n_out), 0, axis)

    big = [f for f in photos if f.lower().endswith((".jpg", ".jpeg", ".cr2"))]
    for photo in big[:2]:
        path = os.path.join(args.samples, photo)
        outs = {}
        for size_name in ("L", "M", "S"):
            d = os.path.join(work, "rs_" + size_name)
            os.makedirs(d, exist_ok=True)
            run([args.cli, "--quiet", "--film", "1", "--efekt", "0", "--ziarno", "0", "--quality", "100", "--size", size_name, "--out", d, path])
            outs[size_name] = np.asarray(Image.open(os.path.join(d, os.listdir(d)[0])).convert("RGB"))
        for size_name in ("M", "S"):
            small = outs[size_name]
            if small.shape == outs["L"].shape:
                continue                                      # the photo is not bigger than this size: unchanged
            h, w = small.shape[:2]
            ref = np.clip(np.rint(area(area(outs["L"], h, 0), w, 1)), 0, 255)
            mse = np.mean((ref - small.astype(np.float64)) ** 2)
            psnr = 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
            check(psnr > 38, "%s size %s: resizer vs exact area average %.1f dB" % (photo, size_name, psnr))
except ImportError:
    print("(numpy / pillow not installed: resizer accuracy not checked)")

if args.hashes:
    with open(args.hashes, "w") as f:
        f.write("\n".join(sorted(hashes, key=lambda s: s.split("  ", 1)[1])) + "\n")
    print("wrote", args.hashes, "(%d lines)" % len(hashes))

print("\n%d problem(s)" % len(fails))
sys.exit(1 if fails else 0)
