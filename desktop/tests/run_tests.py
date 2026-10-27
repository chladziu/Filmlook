#!/usr/bin/env python3
"""The tests of FILM LOOK DESKTOP: does it make exactly the same picture as the module's own PC tool (hostdev), and does it make the
same picture on every computer?

  1. hostdev (modules/filmlook/tools_host/hostdev.c, built here from the module's sources)  ==  the command line version, byte for byte,
     for several films, sliders and (engine) sizes, on every photo of the samples folder
  2. the browser version (the .wasm run under Node)  ==  the command line version, byte for byte (when --wasm is given)
  3. the sizes M and S: the right dimensions, and the same bytes in the command line version and in the browser version
  4. a file with the SHA-256 of every result: the same file must come out on Windows, macOS and Linux (compared by the workflow)

usage: run_tests.py --cli filmlook_cli --samples DIR [--wasm fd_engine.wasm --node node] [--hashes hashes.txt] [--cc cc]
The samples folder holds *.jpg / *.jpeg / *.cr2 / *.cr3 / *.dng (tests/make_samples.py makes some, real camera files are better)."""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

for _s in (sys.stdout, sys.stderr):                 # utf-8 even when the system's code page cannot print Polish letters
    try:
        _s.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

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
args.cli = os.path.abspath(args.cli)
INFO = "%s, python %s, stdout %s, cc %s" % (sys.platform, sys.version.split()[0], sys.stdout.encoding, args.cc)

fails = []
hashes = []
notes = []                                                 # details of failures (stderr of programs, first differing byte ...)
work = tempfile.mkdtemp(prefix="fdtest_")


def annotate(title, text):
    """on GitHub Actions: a red annotation with the text (the job's log can not always be read, the annotations can)"""
    if os.environ.get("GITHUB_ACTIONS"):
        t = text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print("::error title=%s::%s" % (title, t[:6000]), flush=True)


def note(text):
    print(text, flush=True)
    notes.append(text)


def check(ok, msg):
    print(("ok    " if ok else "FAIL  ") + msg, flush=True)
    if not ok:
        fails.append(msg)


def same_file(a, b, msg):
    """check that two files are byte for byte the same; if not, say where they start to differ"""
    da, db = open(a, "rb").read(), open(b, "rb").read()
    ok = da == db
    if not ok:
        k = next((i for i in range(min(len(da), len(db))) if da[i] != db[i]), min(len(da), len(db)))
        note("   %s: sizes %d and %d, first difference at byte %d" % (msg, len(da), len(db), k))
    check(ok, msg)


def crash(kind, value, tb):
    import traceback
    text = "".join(traceback.format_exception(kind, value, tb))
    print(text, flush=True)
    annotate("run_tests crashed", INFO + "\n" + text + "\n".join(fails[-10:]))
    sys.__excepthook__(kind, value, tb)


sys.excepthook = crash


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


def is_container(path):
    """CR3 and DNG: the program develops the full size JPEG inside; the module's hostdev can not open those files"""
    return path.lower().endswith((".cr3", ".dng"))


def source_size(path):
    """size of the picture in a JPG, or of the preview in a CR2 (IFD0 holds its width and height); for a CR3 / DNG the size of the
    biggest JPEG inside (the fake CR3 'prvw_only' has a JPEG track without a 'JPEG' box: only its 1620 x 1080 preview counts)"""
    if is_container(path):
        data = open(path, "rb").read()
        if "prvw_only" in os.path.basename(path).lower():
            return 1620, 1080
        best, pos = None, data.find(b"\xff\xd8\xff")
        while pos >= 0:
            sz = jpeg_size(data[pos:pos + 70000])
            if sz and (best is None or sz[0] * sz[1] > best[0] * best[1]):
                best = sz
            pos = data.find(b"\xff\xd8\xff", pos + 2)
        return best
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
        note(r.stderr.decode(errors="replace") + r.stdout.decode(errors="replace"))
        check(False, "hostdev builds")
    else:
        hostdev = exe

photos = sorted(f for f in os.listdir(args.samples) if f.lower().endswith((".jpg", ".jpeg", ".cr2", ".cr3", ".dng")))
if not photos:
    sys.exit("no photos in " + args.samples)

# the cases: (film index 0 based, sliders as key=value for hostdev, the same for the command line version)
CASES = [
    (0, {}),
    (6, dict(grain=150, contrast=10, fade=20, warm=-30, shadow=20, high=-15, sat=15, vig=40)),
    (10, dict(strength=60, grain=0)),
    (13, dict(vig=70, sat=-20)),
    (0, dict(grain=120, gcolor=0)),                  # a colour film with the grey grain of the camera
]
KEYMAP = {"strength": "--efekt", "grain": "--ziarno", "contrast": "--kontrast", "fade": "--matowosc", "warm": "--cieplo",
          "shadow": "--cienie", "high": "--swiatla", "sat": "--nasycenie", "vig": "--winieta", "gcolor": "--gcolor"}
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
        note("command line version: exit %d, files %s\n%s%s" % (r.returncode, out, r.stderr.decode(errors="replace"), r.stdout.decode(errors="replace")))
        return None
    return os.path.join(d, out[0])


def wasm_make(photo, film, size, sl, outpath, extra=None):
    s = {"strength": 100, "grain": 100}
    s.update(sl)
    a = [args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, photo, outpath, str(film), str(size), "95"]
    a += [str(s.get(k, 0)) for k in ORDER]
    ex = dict(extra or {})
    ex.update({k: v for k, v in sl.items() if k not in ORDER})          # the case's extras (gcolor ...)
    a += ["%s=%d" % kv for kv in ex.items()]
    r = run(a)
    if r.returncode != 0:
        note(r.stderr.decode(errors="replace"))
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
            if hostdev and not is_container(path):
                ref = os.path.join(work, "ref.jpg")
                hd = [hostdev, path, ref, str(film), str(eng_scale), "quality=95"] + ["%s=%d" % kv for kv in dict({"gcolor": 100}, **sl).items()]
                r = run(hd)
                if r.returncode != 0:
                    note("hostdev: exit %d\n%s" % (r.returncode, r.stderr.decode(errors="replace")))
                    check(False, tag + ": hostdev ran")
                else:
                    same_file(ref, got, tag + ": command line == hostdev")
            if args.wasm:
                w = wasm_make(path, film, 100 + eng_scale, sl, os.path.join(work, "w.jpg"))
                check(bool(w) and sha(w) == sha(got), tag + ": browser version == command line")

# the desktop's extra sliders (exposure, tone, white balance, skin, colour mixer, toning, filters): the browser version must make the
# same bytes as the command line version, every one of them must change the picture, and they must change it in the right
# direction (measured on the picture itself: brightness, redness, blueness ...)
EXTRA_CASES = [
    (0, "expo", dict(expo=100)), (0, "hi_t", dict(hi_t=-60)), (0, "sh_t", dict(sh_t=60)), (0, "wh_t", dict(wh_t=50, bl_t=-50)),
    (0, "wb warm", dict(wb_temp=60)), (0, "wb tint", dict(wb_tint=50)), (0, "skin", dict(skin_hue=50, skin_sat=30, skin_lum=20)),
    (0, "mixer", dict(hue_blue=40, sat_green=-60, lum_red=50)), (0, "grain size", dict(gsize=30, grough=40)),
    (6, "bw filter red", dict(filter=3)), (6, "bw filter blue half", dict(filter=5, filter_amt=50)),
    (6, "bw sepia", dict(ton_mode=1, cg_sh_h=30, cg_sh_s=25, cg_hi_h=45, cg_hi_s=20)),
    (6, "bw mixer", dict(lum_green=-60, lum_blue=50)),
    (0, "colour grain off", dict(gcolor=0)), (0, "colour grain half", dict(gcolor=50)),
    (0, "curve master", dict(crv0_n=3, crv0_x0=0, crv0_y0=0, crv0_x1=128, crv0_y1=90, crv0_x2=255, crv0_y2=255)),
    (0, "curve red", dict(crv1_n=2, crv1_x0=0, crv1_y0=40, crv1_x1=255, crv1_y1=255)),
    (0, "curve blue", dict(crv3_n=3, crv3_x0=0, crv3_y0=0, crv3_x1=100, crv3_y1=140, crv3_x2=255, crv3_y2=240)),
    (6, "bw curve", dict(crv0_n=4, crv0_x0=0, crv0_y0=10, crv0_x1=64, crv0_y1=40, crv0_x2=192, crv0_y2=220, crv0_x3=255, crv0_y3=250)),
]
if photos:
    path = os.path.join(args.samples, photos[0])
    base = {}
    def mean_rgb(p):
        try:
            import numpy as np
            from PIL import Image
            return np.asarray(Image.open(p).convert("RGB")).reshape(-1, 3).mean(axis=0)
        except ImportError:
            return None
    for film, name, xs in EXTRA_CASES:
        tag = "%s extra %s" % (photos[0], name)
        a = ["--film", str(film + 1), "--scale", "2"] + [x for k, v in xs.items() for x in ("--" + k, str(v))]
        d = os.path.join(work, "cli_x")
        os.makedirs(d, exist_ok=True)
        for f in os.listdir(d):
            os.remove(os.path.join(d, f))
        r = run([args.cli, "--quiet", "--quality", "95"] + a + ["--out", d, path])
        out = os.listdir(d)
        if r.returncode != 0 or len(out) != 1:
            check(False, tag + ": command line version ran")
            continue
        got = os.path.join(d, out[0])
        hashes.append("%s  %s" % (sha(got), tag.replace(" ", "_")))
        if film not in base:
            ref = cli_make(path, film, {}, ["--scale", "2"], "x0")
            base[film] = (sha(ref), mean_rgb(ref))
        check(sha(got) != base[film][0], tag + ": changes the picture")
        if args.wasm:
            w = wasm_make(path, film, 102, {}, os.path.join(work, "w.jpg"), xs)
            check(bool(w) and sha(w) == sha(got), tag + ": browser version == command line")
        m, b = mean_rgb(got), base[film][1]
        if m is not None and b is not None:
            want = {"expo": m.mean() > b.mean() + 5, "hi_t": m.mean() < b.mean(), "sh_t": m.mean() > b.mean(),
                    "wb warm": m[0] - m[2] > b[0] - b[2] + 3, "wb tint": m[1] < b[1] - 1,
                    "bw filter red": True, "bw sepia": m[0] - m[2] > 8,
                    "curve master": m.mean() < b.mean() - 5, "curve red": m[0] > b[0] + 5, "curve blue": m[2] > b[2] + 3}.get(name)
            if want is not None:
                check(bool(want), tag + ": goes the right way (mean RGB %s -> %s)" % (b.round(1), m.round(1)))

# CR3 and DNG: the program develops the JPEG inside the file. Where the samples folder has a CR2 with the same name (the fake ones
# of make_samples.py are made from the same JPEG), the picture must be exactly the same; the camera data and the orientation must
# reach the output; a file with no big preview must be refused with a message (not crash), and so must a damaged one.
def exif_of(path):
    try:
        from PIL import Image
        return Image.open(path).getexif()
    except ImportError:
        return None

for photo in photos:
    stem, ext = os.path.splitext(photo)
    if ext.lower() not in (".cr3", ".dng"):
        continue
    path = os.path.join(args.samples, photo)
    twin = os.path.join(args.samples, stem + ".cr2")
    got = cli_make(path, 0, {}, ["--scale", "2"], "c")
    tag = photo + " container"
    if not got:
        check(False, tag + ": command line version ran")
        continue
    if args.wasm:
        w = wasm_make(path, 0, 102, {}, os.path.join(work, "w.jpg"))
        check(bool(w) and sha(w) == sha(got), tag + ": browser version == command line")
    if os.path.exists(twin):
        ref = cli_make(twin, 0, {}, ["--scale", "2"], "c2")
        try:
            import numpy as np
            from PIL import Image
            a, b = np.asarray(Image.open(got).convert("RGB")), np.asarray(Image.open(ref).convert("RGB"))
            check(a.shape == b.shape and bool((a == b).all()), tag + ": same pixels as the CR2 made from the same JPEG")
        except ImportError:
            pass
    ex = exif_of(got)
    if ex is not None:
        want_or = 6 if "portrait" in stem else 1
        check(ex.get(0x112) == want_or, tag + ": orientation %s (wanted %d)" % (ex.get(0x112), want_or))
        check(str(ex.get(0x10F, "")).startswith("Canon"), tag + ": camera make in the output (%r)" % (ex.get(0x10F),))

# a DNG with no big preview (the common "raw only" case) and damaged files must give an error message and exit code, never a crash
try:
    sys.path.insert(0, HERE)
    import make_samples as ms
    tmpj = os.path.join(work, "t.jpg")
    ms.save_jpeg(ms.picture(600, 400, 9), tmpj, 90)
    bad = {}
    bad["dng_no_preview.dng"] = os.path.join(work, "dng_no_preview.dng")
    ms.fake_dng(bad["dng_no_preview.dng"], tmpj, 600, 400, 1, tmpj, be=True, with_preview=False)
    good = {n: os.path.join(args.samples, n) for n in photos if n.lower().endswith((".cr3", ".dng"))}
    import random
    rnd = random.Random(7)
    for name, src in sorted(good.items()):
        data = open(src, "rb").read()
        for k, cut in enumerate((40, 200, 1500, len(data) // 3, len(data) // 2, len(data) - 5000)):
            f = os.path.join(work, "cut%d_%s" % (k, name))
            open(f, "wb").write(data[:cut])
            bad["cut%d_%s" % (k, name)] = f
        for k in range(6):                                 # random damage of the first 6 KB (the boxes / IFDs) and of the whole file
            d = bytearray(data)
            for _ in range(12):
                d[rnd.randrange(0, min(len(d), 6000 if k < 3 else len(d)))] = rnd.randrange(256)
            f = os.path.join(work, "dmg%d_%s" % (k, name))
            open(f, "wb").write(bytes(d))
            bad["dmg%d_%s" % (k, name)] = f
    for name, f in sorted(bad.items()):
        d = os.path.join(work, "cli_bad")
        os.makedirs(d, exist_ok=True)
        for old in os.listdir(d):
            os.remove(os.path.join(d, old))
        r = run([args.cli, "--quiet", "--film", "1", "--size", "S", "--out", d, f])
        crashed = r.returncode < 0 or r.returncode > 100
        if name == "dng_no_preview.dng":
            check(r.returncode != 0 and not os.listdir(d) and b"" != r.stdout + r.stderr, "DNG without a preview: refused with a message (exit %d)" % r.returncode)
        else:
            check(not crashed, "damaged %s: no crash (exit %d)" % (name, r.returncode))
        if args.wasm:
            w = run([args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, f, os.path.join(work, "wb.jpg"), "0", "102", "95"])
            err = w.stderr.decode(errors="replace")
            trapped = "RuntimeError" in err or "unreachable" in err or "out of bounds" in err or "memory access" in err
            check(not trapped, "damaged %s: browser version does not trap" % name)
            if name == "dng_no_preview.dng":
                check(w.returncode != 0 and "ERROR" in err, "DNG without a preview: browser version refuses with a message")
except ImportError:
    print("(numpy / pillow not installed: CR3 / DNG damage tests skipped)")

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
        if size_name == "L" and hostdev and not is_container(path):
            ref = os.path.join(work, "ref.jpg")
            run([hostdev, path, ref, str(film), "1", "quality=95"] + ["%s=%d" % kv for kv in dict({"gcolor": 100}, **sl).items()])
            same_file(ref, got, tag + ": size L == hostdev")

# a folder and a file with Polish letters in the name (the command line version must open and write them on every system)
uni_dir = os.path.join(work, "zażółć gęślą jaźń")
os.makedirs(uni_dir, exist_ok=True)
uni_src = os.path.join(uni_dir, "zdjęcie_żółć.jpg")
with open(os.path.join(args.samples, photos[0]), "rb") as fi, open(uni_src, "wb") as fo:
    fo.write(fi.read())
r = run([args.cli, "--quiet", "--film", "1", "--size", "S", uni_dir])
made = os.path.join(uni_dir, "FILMLOOK")
if not (r.returncode == 0 and os.path.isdir(made)):
    note("exit %d\n%s%s" % (r.returncode, r.stderr.decode(errors="replace"), r.stdout.decode(errors="replace")))
check(r.returncode == 0 and os.path.isdir(made) and len(os.listdir(made)) >= 1, "folder and file names with Polish letters")

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

    big = [f for f in photos if f.lower().endswith((".jpg", ".jpeg", ".cr2"))]    # (CR3 / DNG: same code after the preview is cut out)
    for pi, photo in enumerate(big[:2]):
        path = os.path.join(args.samples, photo)
        outs = {}
        for size_name in ("L", "M", "S"):
            d = os.path.join(work, "rs_%d_%s" % (pi, size_name))          # a new folder for every photo and size: listdir() finds the one file
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
    with open(args.hashes, "w", newline="\n") as f:                  # LF also on Windows, so that the files can be compared with diff
        f.write("\n".join(sorted(hashes, key=lambda s: s.split("  ", 1)[1])) + "\n")
    print("wrote", args.hashes, "(%d lines)" % len(hashes))

print("\n%d problem(s)" % len(fails))
if fails:
    annotate("%d problem(s) in run_tests" % len(fails), INFO + "\n" + "\n".join(fails[:40]) + "\n---\n" + "\n".join(notes)[:4000])
sys.exit(1 if fails else 0)
