#!/usr/bin/env python3
"""The tests of FILM LOOK DESKTOP: does it make exactly the same picture as the module's own PC tool (hostdev), and does it make the
same picture on every computer?

  1. hostdev (modules/filmlook/tools_host/hostdev.c, built here from the module's sources)  ==  the command line version, byte for byte,
     for several films, sliders and (engine) sizes, on every photo of the samples folder
  2. the browser version (the .wasm run under Node)  ==  the command line version, byte for byte (when --wasm is given)
  3. the sizes M and S: the right dimensions, and the same bytes in the command line version and in the browser version
  4. RAW: files with real sensor data (CR2 of the 5D Mark II, DNG of several kinds; tests/rawgen.py): the command line version and the
     browser version make the same bytes, every kind of DNG gives the same pixels as the CR2, the picture comes back (close to the one
     the sensor data was made from), the switches "JPG z pliku" / "wzorzec z JPG" / "najwyższa jakość" work, damaged files do not crash;
     CR3 of the Canon EOS RP (tests/crxgen.py codes the sensor data like Canon's CRX: lossless in tiles, C-RAW with and without loss):
     the decoder (tests/cr3_probe.c, built here with the sanitizers where the compiler has them) gives back every sensor value exactly,
     the lossless and the C-RAW files develop to the same pixels, damaged copies never crash it
  5. the crop and the size of the result: every kind of file, landscape and portrait, a crop of any shape, an exact ratio, any size
     (smaller and bigger than the photo), the same bytes in the command line version and in the browser version
  6. a file with the SHA-256 of every result: the same file must come out on Windows, macOS and Linux (compared by the workflow)

The comparisons with the module's hostdev (1) and the sizes use the camera's way: --usejpg 1 (the JPEG inside the file, not the sensor
data) and --hq 0 (JPEG 4:2:0, colour table), because that is all the camera's engine does.

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
        if os.path.basename(path).startswith("synth_raw_cr3_"):          # (the coded sensor data is no place to look for a JPEG: the picture is 600 x 400)
            return 600, 400
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


LEGACY = ["--hq", "0", "--usejpg", "1"]            # the way of the camera's engine (see the top)


def cli_make(photo, film, sl, extra, outname, legacy=True, quality=95):
    d = os.path.join(work, "cli_" + outname)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        try:
            os.remove(os.path.join(d, f))
        except OSError:                              # Windows: a picture that the test still has open keeps its file: another folder
            cli_make.n = getattr(cli_make, "n", 0) + 1
            d = os.path.join(work, "cli_%s_%d" % (outname, cli_make.n))
            os.makedirs(d, exist_ok=True)
            break
    r = run([args.cli, "--quiet", "--quality", str(quality)] + cli_args(film, sl) + (LEGACY if legacy else []) + extra + ["--out", d, photo])
    out = os.listdir(d)
    if r.returncode != 0 or len(out) != 1:
        note("command line version: exit %d, files %s\n%s%s" % (r.returncode, out, r.stderr.decode(errors="replace"), r.stdout.decode(errors="replace")))
        return None
    return os.path.join(d, out[0])


def wasm_make(photo, film, size, sl, outpath, extra=None, legacy=True, quality=95):
    s = {"strength": 100, "grain": 100}
    s.update(sl)
    a = [args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, photo, outpath, str(film), str(size), str(quality)]
    a += [str(s.get(k, 0)) for k in ORDER]
    ex = dict({"hq": 0, "usejpg": 1} if legacy else {})
    ex.update(extra or {})
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
        r = run([args.cli, "--quiet", "--quality", "95"] + LEGACY + a + ["--out", d, path])
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
        with Image.open(path) as im:
            return im.getexif()
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

# ------------------------------------------------------------------ RAW: the sensor data of CR2 / DNG files (tests/rawgen.py makes them)
# The files hold one picture. What has to hold: the browser version makes the same bytes as the command line version (every size); the
# CR2 and every Bayer DNG (strips, lossless tiles, big endian, a cropped active area) give the very same pixels; the picture comes back
# (the sensor data was made from it); "JPG z pliku" / "wzorzec z JPG" / "najwyższa jakość" each change the result in their own way;
# a portrait shot is marked as such; damaged files never crash.
RAW_FILES = [f for f in photos if f.startswith("synth_raw_")]
if RAW_FILES:
    try:
        import numpy as np
        from PIL import Image, JpegImagePlugin
    except ImportError:
        np = None
        print("(numpy / pillow not installed: RAW tests skipped)")
    if np is not None:
        ref_png = os.path.join(args.samples, "_raw_picture.png")
        picture_ref = np.asarray(Image.open(ref_png).convert("RGB")).astype(np.float64) if os.path.exists(ref_png) else None

        def px(path):
            return np.asarray(Image.open(path).convert("RGB")).astype(np.int32)

        raw_out = {}
        for photo in RAW_FILES:
            path = os.path.join(args.samples, photo)
            tag = photo + " RAW"
            got = cli_make(path, 0, {}, ["--size", "L"], "r1", legacy=False)
            if not got:
                check(False, tag + ": command line version ran")
                continue
            raw_out[photo] = got
            hashes.append("%s  %s" % (sha(got), tag.replace(" ", "_")))
            # (the hashes of the command line version are kept on every system, so that the systems can be compared; the browser version only where there is one)
            for code in (0, 102):
                c = got if code == 0 else cli_make(path, 0, {}, ["--scale", "2"], "r2", legacy=False)
                if args.wasm:
                    w = wasm_make(path, 0, code, {}, os.path.join(work, "w.jpg"), legacy=False)
                    check(bool(w) and bool(c) and sha(w) == sha(c), "%s size %s: browser version == command line" % (tag, "L" if code == 0 else "half"))
                if c and code:
                    hashes.append("%s  %s" % (sha(c), (tag + "_half").replace(" ", "_")))
            for size_name, code in (("M", 1), ("S", 2)):
                c = cli_make(path, 6, dict(grain=150, vig=40, warm=-20), ["--size", size_name, "--expo", "30", "--wb_temp", "15", "--crv0_n", "3", "--crv0_x0", "0", "--crv0_y0", "0", "--crv0_x1", "128", "--crv0_y1", "110", "--crv0_x2", "255", "--crv0_y2", "255"], "r3", legacy=False)
                if args.wasm:
                    w = wasm_make(path, 6, code, dict(grain=150, vig=40, warm=-20), os.path.join(work, "w.jpg"), dict(expo=30, wb_temp=15, crv0_n=3, crv0_x0=0, crv0_y0=0, crv0_x1=128, crv0_y1=110, crv0_x2=255, crv0_y2=255), legacy=False)
                    check(bool(w) and bool(c) and sha(w) == sha(c), "%s size %s with sliders: browser version == command line" % (tag, size_name))
                if c:
                    hashes.append("%s  %s" % (sha(c), ("%s size %s sliders" % (tag, size_name)).replace(" ", "_")))
            im = Image.open(got)
            want_or = 6 if "portrait" in photo else 1
            check(im.getexif().get(0x112) == want_or, "%s: orientation %s (wanted %d)" % (tag, im.getexif().get(0x112), want_or))
            check(JpegImagePlugin.get_sampling(im) == 0, "%s: the JPEG has full resolution colours (4:4:4)" % tag)
            check(im.size == (600, 400), "%s: size %s (wanted (600, 400))" % (tag, im.size))
            im.close()

        # the picture comes back: the film at Efekt 0, no grain, quality 100
        plain_n = [0]

        def plain(photo, extra):
            plain_n[0] += 1                                  # every call has its own folder: the results must not overwrite each other
            d = os.path.join(work, "rp%d" % plain_n[0])
            os.makedirs(d, exist_ok=True)
            r = run([args.cli, "--quiet", "--film", "1", "--efekt", "0", "--ziarno", "0", "--quality", "100", "--size", "L", "--out", d, os.path.join(args.samples, photo)] + extra)
            return os.path.join(d, os.listdir(d)[0]) if r.returncode == 0 and os.listdir(d) else None

        cr2 = "synth_raw_5d2_landscape.cr2"
        if cr2 in raw_out:
            base = plain(cr2, [])
            jpg = plain(cr2, ["--usejpg", "1"])
            noref = plain(cr2, ["--rawref", "0"])
            lowq = plain(cr2, ["--hq", "0"])
            check(bool(base) and bool(jpg) and sha(base) != sha(jpg), "RAW: 'JPG z pliku' gives another picture than the sensor data")
            check(bool(base) and bool(noref) and sha(base) != sha(noref), "RAW: the pattern of the camera's JPEG changes the tone")
            check(bool(base) and bool(lowq) and sha(base) != sha(lowq), "RAW: the best quality switch changes the file")
            if base and picture_ref is not None:
                a = px(base)
                mae = float(np.abs(a - picture_ref).mean())
                check(mae < 14, "RAW: the picture comes back from the sensor data (mean error %.1f levels)" % mae)
                if jpg:
                    maej = float(np.abs(px(jpg) - picture_ref).mean())
                    check(maej < 14, "RAW: 'JPG z pliku' gives the preview (mean error %.1f)" % maej)
                if noref:
                    mn = float(np.abs(px(noref) - picture_ref).mean())
                    note("   (without the pattern of the JPG the mean error is %.1f: the standard tone of the program is not the camera's)" % mn)
            if lowq:
                with Image.open(lowq) as lq:
                    check(JpegImagePlugin.get_sampling(lq) == 2, "the camera's way (hq 0) writes 4:2:0")
                check(os.path.getsize(base) > os.path.getsize(lowq), "the best quality file is the bigger one (%d > %d bytes)" % (os.path.getsize(base), os.path.getsize(lowq)))
            # the same pixels from every Bayer file
            if base:
                a = px(base)
                for other in ("synth_raw_strips.dng", "synth_raw_tiles.dng", "synth_raw_crop.dng"):
                    if other in raw_out:
                        o = plain(other, [])
                        check(bool(o) and px(o).shape == a.shape and bool((px(o) == a).all()), "%s: the very same pixels as the CR2" % other)
                if "synth_raw_linear.dng" in raw_out:
                    o = plain("synth_raw_linear.dng", [])
                    if o and picture_ref is not None:
                        ml = float(np.abs(px(o) - picture_ref).mean())
                        check(ml < 14, "synth_raw_linear.dng: the picture comes back (mean error %.1f)" % ml)
            # the portrait file: the same pixels as the landscape one, and the orientation in the EXIF
            if "synth_raw_5d2_portrait.cr2" in raw_out and base:
                o = plain("synth_raw_5d2_portrait.cr2", [])
                check(bool(o) and bool((px(o) == px(base)).all()), "portrait CR2: the pixels are the same, only the orientation mark differs")

        # CR3 of the Canon EOS RP: the decoder of CRX (src/fd_cr3.h) against the exact sensor arrays that tests/crxgen.py coded
        import random
        cr3_exact = [("lossless", "lossless in four tiles"), ("craw", "C-RAW without loss, the maker note of the Canon box only"), ("portrait", "C-RAW with a quantization")]
        if all(("synth_raw_cr3_%s.cr3" % n) in RAW_FILES for n, _ in cr3_exact):
            src_dir = os.path.join(os.path.dirname(HERE), "src")
            probe = os.path.join(work, "cr3_probe" + (".exe" if os.name == "nt" else ""))
            sanitized = False
            for san in (["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"], []):
                r = run([args.cc, "-O1", "-g", "-std=gnu99"] + san + ["-DSE_HOSTED=1", "-I", src_dir, "-I", MODULE, "-o", probe,
                                                                    os.path.join(HERE, "cr3_probe.c"), os.path.join(src_dir, "fd_engine.c")])
                if r.returncode == 0:
                    sanitized = bool(san)
                    break
                note(r.stderr.decode(errors="replace")[-1500:])
            else:
                probe = None
            check(probe is not None, "the CR3 decoder builds on its own (%s)" % ("with the address and undefined behaviour sanitizers" if sanitized else "without sanitizers"))
            penv = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:exitcode=77", UBSAN_OPTIONS="halt_on_error=1:exitcode=78")
            if probe:
                for n, what in cr3_exact:
                    f = os.path.join(args.samples, "synth_raw_cr3_%s.cr3" % n)
                    want = os.path.join(args.samples, "_cr3_%s.u16" % n)
                    got_raw = os.path.join(work, "cr3_%s.u16" % n)
                    r = run([probe, f, got_raw], env=penv)
                    same = r.returncode == 0 and os.path.exists(got_raw) and os.path.exists(want) and open(got_raw, "rb").read() == open(want, "rb").read()
                    if not same:
                        note("   probe exit %d\n%s%s" % (r.returncode, r.stdout.decode(errors="replace")[-600:], r.stderr.decode(errors="replace")[-1500:]))
                    check(same, "CR3 %s: the decoder gives back every sensor value" % what)
                    if same:
                        hashes.append("%s  CR3_sensor_values_%s" % (sha(got_raw), n))
                r = run([probe, os.path.join(args.samples, "synth_raw_cr3_unknown.cr3")], env=penv)
                check(r.returncode == 1, "CR3 of a camera without a colour matrix: the RAW decoder refuses it, the JPEG is taken (exit %d)" % r.returncode)
                # damaged copies: the metadata, the tile headers, the coded data, cut files; never a crash or a memory error
                rnd3 = random.Random(23)
                for n, what in cr3_exact:
                    data = open(os.path.join(args.samples, "synth_raw_cr3_%s.cr3" % n), "rb").read()
                    start = data.find(b"\xff\x01\x00\x08", 1000)
                    codes, badn = {}, 0
                    for k in range(90):
                        d = bytearray(data)
                        mode = k % 5
                        if mode == 0:
                            for _ in range(rnd3.randint(1, 12)):
                                d[rnd3.randrange(len(d))] = rnd3.randrange(256)
                        elif mode == 1:
                            for _ in range(rnd3.randint(1, 8)):
                                d[rnd3.randrange(0, start)] = rnd3.randrange(256)
                        elif mode == 2:
                            for _ in range(rnd3.randint(1, 6)):
                                d[start + rnd3.randrange(0, 700)] = rnd3.randrange(256)
                        elif mode == 3:
                            for _ in range(rnd3.randint(1, 40)):
                                d[start + 700 + rnd3.randrange(0, len(d) - start - 700)] = rnd3.randrange(256)
                        else:
                            d = d[:rnd3.randrange(30, len(d))]
                        fz = os.path.join(work, "cr3_fuzz.cr3")
                        open(fz, "wb").write(bytes(d))
                        r = run([probe, fz], env=penv)
                        codes[r.returncode] = codes.get(r.returncode, 0) + 1
                        if r.returncode not in (0, 1, 2):
                            badn += 1
                            keep = os.path.join(work, "cr3_fuzz_bad_%s_%d.cr3" % (n, k))
                            open(keep, "wb").write(bytes(d))
                            note("   damaged copy %d (mode %d) of %s: exit %d\n%s" % (k, mode, n, r.returncode, r.stderr.decode(errors="replace")[-1500:]))
                    check(badn == 0, "CR3 %s: 90 damaged copies through the decoder, no crash and no memory error (exits %s)" % (what, sorted(codes.items())))
            # the developed pictures
            pl, pc, pp, pu = (plain("synth_raw_cr3_%s.cr3" % n, []) for n in ("lossless", "craw", "portrait", "unknown"))
            if pl and pc:
                check(bool((px(pl) == px(pc)).all()), "CR3: the C-RAW file without loss develops to the very same pixels as the lossless file")
            if picture_ref is not None:
                for o, what in ((pl, "lossless"), (pc, "C-RAW"), (pp, "C-RAW with a quantization")):
                    if o:
                        m3 = float(np.abs(px(o) - picture_ref).mean())
                        check(m3 < 14, "CR3 %s: the picture comes back from the sensor data (mean error %.1f levels)" % (what, m3))
            if pl and pp:
                dq = float(np.abs(px(pl) - px(pp)).mean())
                check(dq < 3, "CR3: the quantization of the C-RAW changes the pictures only a little (%.2f levels)" % dq)
            jl = plain("synth_raw_cr3_lossless.cr3", ["--usejpg", "1"])
            if pl and jl:
                check(sha(pl) != sha(jl), "CR3: 'JPG z pliku' gives another picture than the sensor data")
            if pu:
                ju = plain("synth_raw_cr3_unknown.cr3", ["--usejpg", "1"])
                if ju:
                    check(sha(pu) == sha(ju), "CR3 of a camera without a colour matrix: develops from its JPEG, as before")

        # damaged RAW files: no crash (the command line), no trap (the browser version)
        import random
        rnd = random.Random(11)
        for name in RAW_FILES:
            data = open(os.path.join(args.samples, name), "rb").read()
            cases = []
            for k, cut in enumerate((40, 300, 2000, len(data) // 3, len(data) // 2, len(data) - 30000, len(data) - 100)):
                cases.append(("cut%d_%s" % (k, name), data[:cut]))
            for k in range(8):                              # random damage: the first 8 KB (tags) and the whole file (the sensor data)
                d = bytearray(data)
                for _ in range(10 if k < 4 else 40):
                    d[rnd.randrange(0, min(len(d), 8000 if k < 4 else len(d)))] = rnd.randrange(256)
                cases.append(("dmg%d_%s" % (k, name), bytes(d)))
            for cname, blob in cases:
                f = os.path.join(work, cname)
                open(f, "wb").write(blob)
                d = os.path.join(work, "cli_bad2")
                os.makedirs(d, exist_ok=True)
                for old in os.listdir(d):
                    os.remove(os.path.join(d, old))
                r = run([args.cli, "--quiet", "--film", "1", "--size", "S", "--out", d, f])
                check(not (r.returncode < 0 or r.returncode > 100), "damaged %s: no crash (exit %d)" % (cname, r.returncode))
                if args.wasm:
                    w = run([args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, f, os.path.join(work, "wb.jpg"), "0", "102", "95"])
                    err = w.stderr.decode(errors="replace")
                    trapped = "RuntimeError" in err or "unreachable" in err or "out of bounds" in err or "memory access" in err
                    check(not trapped, "damaged %s: browser version does not trap" % cname)


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

# ------------------------------------------------------------------ the crop and the size of the result (any resolution, any shape)
# The page has a crop tool (free or with a locked ratio) and "Rozmiar zapisu: pełny / własny". Every kind of file (JPG, CR2 with sensor
# data, CR3, DNG; landscape and portrait with the orientation mark) must give: the right pixels (a crop is the same picture as that part
# of the whole one), the exact ratio, the exact size, and the browser version must make the same bytes as the command line version.
try:
    import numpy as np
    from PIL import Image, ImageOps
    GEO_FILES = [f for f in ("synth_raw_5d2_landscape.cr2", "synth_raw_5d2_portrait.cr2", "synth_raw_strips.dng", "synth_landscape.cr3",
                             "synth_raw_cr3_portrait.cr3", "synth_portrait.cr2", "synth_small.jpg") if f in photos]
    geo_n = [0]

    def geo_make(photo, opts, quality=100, plain=True):
        """the picture as a file; Efekt 0 and no grain (plain) so that a crop can be compared with the same part of the whole picture"""
        geo_n[0] += 1
        d = os.path.join(work, "geo%d" % geo_n[0])
        os.makedirs(d, exist_ok=True)
        r = run([args.cli, "--quiet", "--film", "1", "--quality", str(quality)] + (["--efekt", "0", "--ziarno", "0"] if plain else []) + opts + ["--out", d, os.path.join(args.samples, photo)])
        out = os.listdir(d)
        if r.returncode != 0 or len(out) != 1:
            note("geometry %s %s: exit %d\n%s" % (photo, opts, r.returncode, r.stderr.decode(errors="replace")))
            return None
        return os.path.join(d, out[0])

    def shown(path):                                     # the picture as it is shown (the camera's orientation applied): pixels, (w, h)
        with Image.open(path) as src:
            ori = src.getexif().get(0x112)
            im = src.convert("RGB")
        if ori in (2, 3, 4, 5, 6, 7, 8):
            im = ImageOps.exif_transpose(im)
        return np.asarray(im).astype(np.int32)

    def frac(f, n):
        return (int(f * 1000000) * n + 500000) // 1000000

    for photo in GEO_FILES:
        full = geo_make(photo, [])
        if not full:
            check(False, photo + " geometry: the whole picture was made")
            continue
        A = shown(full)
        H, W = A.shape[:2]
        tag = photo + " geometry"
        # a crop in fractions of the picture as it is shown: size, and the same pixels as that part of the whole picture
        c = geo_make(photo, ["--crop", "0.25,0.1,0.75,0.6"])
        x0, x1, y0, y1 = frac(0.25, W), frac(0.75, W), frac(0.1, H), frac(0.6, H)
        if c:
            B = shown(c)
            check(B.shape[:2] == (y1 - y0, x1 - x0), "%s: crop size %s (wanted %s)" % (tag, B.shape[1::-1], (x1 - x0, y1 - y0)))
            if B.shape == A[y0:y1, x0:x1].shape:
                mae = float(np.abs(B - A[y0:y1, x0:x1]).mean())
                check(mae < 1.8, "%s: the crop is that part of the whole picture (mean error %.2f)" % (tag, mae))
        else:
            check(False, tag + ": crop made")
        # the whole picture as a crop, or a rectangle with nothing in it: the same file as without a crop
        for opts, what in ((["--crop", "0,0,1,1"], "the whole picture as a crop"), (["--crop", "0.5,0.5,0.4,0.4"], "an empty crop")):
            w = geo_make(photo, opts)
            check(bool(w) and sha(w) == sha(full), "%s: %s == no crop, byte for byte" % (tag, what))
        # the ratio: exact, in the middle; for a photo that lies the other way the ratio turns round, "!" keeps it as written
        for ratio, rw, rh in (("1:1", 1, 1), ("3:2", 3, 2), ("4:5", 4, 5), ("16:9", 16, 9)):
            r1 = geo_make(photo, ["--ratio", ratio])
            if not r1:
                check(False, "%s: ratio %s made" % (tag, ratio))
                continue
            B = shown(r1)
            h, w = B.shape[:2]
            # "3:2" is long:short; on a photo that lies the other way the engine turns it round (a square photo keeps it)
            want_swap = rw != rh and ((W < H) != (rw < rh))
            ea, eb = (rh, rw) if want_swap else (rw, rh)
            ok = (w * eb == h * ea)
            check(ok, "%s: ratio %s is exact (%d x %d)" % (tag, ratio, w, h))
            ax0, ay0 = (W - w) // 2, (H - h) // 2
            if abs((W - w) - 2 * ax0) <= 1 and B.shape == A[ay0:ay0 + h, ax0:ax0 + w].shape:
                mae = float(np.abs(B - A[ay0:ay0 + h, ax0:ax0 + w]).mean())
                check(mae < 1.8, "%s: ratio %s: the middle of the picture (mean error %.2f)" % (tag, ratio, mae))
        r2 = geo_make(photo, ["--ratio", "16:9!"])
        if r2:
            B = shown(r2)
            check(B.shape[1] * 9 == B.shape[0] * 16, "%s: ratio 16:9! stays 16:9 (%d x %d)" % (tag, B.shape[1], B.shape[0]))
        # the size: the long side, the width, the height; smaller and bigger than the photo
        L0 = max(W, H)
        for opts, want in ((["--long", "300"], (300, None)), (["--long", str(2 * L0)], (2 * L0, None)), (["--width", "257"], (257, None)), (["--height", "199"], (None, 199)),
                           (["--width", str(W + 37)], (W + 37, None))):
            r3 = geo_make(photo, opts)
            if not r3:
                check(False, "%s: %s made" % (tag, " ".join(opts)))
                continue
            B = shown(r3)
            h, w = B.shape[:2]
            if opts[0] == "--long":
                okd = max(w, h) == want[0] and abs(w * H - h * W) <= max(W, H)
            elif want[0] is not None:
                okd = w == want[0] and abs(h - round(H * w / W)) <= 1
            else:
                okd = h == want[1] and abs(w - round(W * h / H)) <= 1
            check(okd, "%s: %s gives %d x %d" % (tag, " ".join(opts), w, h))
            if opts[0] in ("--long", "--width") and w > W and h > H:         # enlarged: against an independent bicubic (the same filter, on the tone-mapped picture)
                ref = np.asarray(Image.fromarray(A.astype(np.uint8)).resize((w, h), Image.BICUBIC)).astype(np.int32)
                mae = float(np.abs(ref - B).mean())
                check(mae < 3.0, "%s: enlarged to %d x %d, mean error against bicubic %.2f" % (tag, w, h, mae))
        # a crop and a size together, the part cut out and made bigger
        r4 = geo_make(photo, ["--crop", "0.2,0.2,0.6,0.7", "--ratio", "1:1", "--long", "500"], plain=False)
        if r4:
            B = shown(r4)
            check(B.shape[:2] == (500, 500), "%s: crop + ratio 1:1 + long 500 -> %s" % (tag, B.shape[1::-1]))
        # too big: refused with a message, no crash
        r5 = run([args.cli, "--quiet", "--film", "1", "--long", "20000", "--out", os.path.join(work, "geo_big"), os.path.join(args.samples, photo)])
        check(r5.returncode == 1 and b"za du" in r5.stderr, "%s: 20000 px is refused with a message (exit %d)" % (tag, r5.returncode))

        # straightening (the angle of the crop tool): the crop is a rectangle of the turned picture
        import math
        r0 = geo_make(photo, ["--angle", "0"])
        check(bool(r0) and sha(r0) == sha(full), "%s: angle 0 == no angle, byte for byte" % tag)
        ang = 4.5
        rx0, ry0, rx1, ry1 = 0.2, 0.25, 0.8, 0.75
        ra = geo_make(photo, ["--angle", str(ang), "--crop", "%g,%g,%g,%g" % (rx0, ry0, rx1, ry1)])
        if ra:
            B = shown(ra)
            px0, px1, py0, py1 = frac(rx0, W), frac(rx1, W), frac(ry0, H), frac(ry1, H)
            check(B.shape[:2] == (py1 - py0, px1 - px0), "%s: angle %g + crop: size %s (wanted %s)" % (tag, ang, B.shape[1::-1], (px1 - px0, py1 - py0)))
            th = math.radians(ang)
            cs_, sn_ = math.cos(th), math.sin(th)
            coef = (cs_, sn_, W / 2 + cs_ * (px0 - W / 2) + sn_ * (py0 - H / 2), -sn_, cs_, H / 2 - sn_ * (px0 - W / 2) + cs_ * (py0 - H / 2))
            ref = np.asarray(Image.fromarray(A.astype(np.uint8)).transform((px1 - px0, py1 - py0), Image.AFFINE, coef, resample=Image.BICUBIC)).astype(np.int32)
            if ref.shape == B.shape:
                mae = float(np.abs(ref - B).mean())
                check(mae < 3.0, "%s: angle %g: the same as an independent bicubic turn of the whole picture (mean error %.2f)" % (tag, ang, mae))
                flat = float(np.abs(ref[8:-8, 8:-8] - B[8:-8, 8:-8]).mean())
                check(flat < 3.0, "%s: angle %g: inside the picture too (mean error %.2f)" % (tag, ang, flat))
                turned = float(np.abs(B - A[py0:py1, px0:px1]).mean())
                check(turned > 1.0 or A[py0:py1, px0:px1].std() < 3, "%s: angle %g really turns the picture (differs from the plain crop by %.2f)" % (tag, ang, turned))
        else:
            check(False, "%s: angle %g + crop made" % (tag, ang))
        # an angle without a crop: the biggest rectangle of the shape of the picture that lies inside the turned one
        for ang2 in (3.0, -10.0, 30.0):
            rb = geo_make(photo, ["--angle", str(ang2)])
            if not rb:
                check(False, "%s: angle %g made" % (tag, ang2))
                continue
            B = shown(rb)
            h, w = B.shape[:2]
            th = math.radians(abs(ang2))
            k = min(1 / (math.cos(th) + (H / W) * math.sin(th)), 1 / ((W / H) * math.sin(th) + math.cos(th)))
            check(abs(w - W * k) <= 3 and abs(h - H * k) <= 3 and abs(w * H - h * W) <= max(W, H), "%s: angle %g without a crop gives %d x %d (about %d x %d, the shape of the picture)" % (tag, ang2, w, h, W * k, H * k))
        rc1 = geo_make(photo, ["--angle", "10"])
        rc2 = geo_make(photo, ["--angle", "10", "--crop", "0,0,1,1"])
        check(bool(rc1) and bool(rc2) and sha(rc1) == sha(rc2), "%s: angle 10 with the whole picture as a crop is shrunk to fit == no crop, byte for byte" % tag)
        # a crop that does not fit into the turned picture is shrunk round its centre, an exact ratio stays exact
        rd = geo_make(photo, ["--angle", "-6", "--crop", "0,0.1,1,0.9", "--ratio", "16:9!"])
        if rd:
            B = shown(rd)
            check(B.shape[1] * 9 == B.shape[0] * 16, "%s: angle -6 + ratio 16:9! stays exact (%d x %d)" % (tag, B.shape[1], B.shape[0]))
        # quarter turns: only the orientation mark changes; the picture is the same, turned
        for tn, kk in ((1, -1), (2, 2), (3, 1)):
            rt = geo_make(photo, ["--turn", str(tn)])
            if not rt:
                check(False, "%s: turn %d made" % (tag, tn))
                continue
            B = shown(rt)
            want = np.rot90(A, k=kk)
            check(B.shape == want.shape, "%s: turn %d gives %s (wanted %s)" % (tag, tn, B.shape[1::-1], want.shape[1::-1]))
            if B.shape == want.shape:
                mae = float(np.abs(B - want).mean())
                check(mae < 1.8, "%s: turn %d is the picture turned by %d degrees clockwise (mean error %.2f)" % (tag, tn, 90 * tn, mae))
        rt4 = geo_make(photo, ["--turn", "4"])
        check(bool(rt4) and sha(rt4) == sha(full), "%s: turn 4 == no turn, byte for byte" % tag)
        # a turn, an angle, a ratio and a size together
        re_ = geo_make(photo, ["--turn", "1", "--angle", "-7", "--ratio", "3:2", "--long", "700"], plain=False)
        if re_:
            B = shown(re_)
            check(max(B.shape[:2]) == 700 and abs(B.shape[0] * 3 - B.shape[1] * 2) <= 3 or abs(B.shape[1] * 3 - B.shape[0] * 2) <= 3, "%s: turn 1 + angle -7 + ratio 3:2 + long 700 -> %s" % (tag, B.shape[1::-1]))

    # the same bytes from the browser version (where there is one); the hashes of the command line version are kept on every system
    if True:
        combos = [
            (dict(crop_x0=100000, crop_y0=200000, crop_x1=700000, crop_y1=900000, crop_ar_w=4, crop_ar_h=3, crop_ar_auto=1, out_long=900),
             ["--crop", "0.1,0.2,0.7,0.9", "--ratio", "4:3", "--long", "900"]),
            (dict(out_long=1500), ["--long", "1500"]),
            (dict(crop_ar_w=1, crop_ar_h=1), ["--ratio", "1:1"]),
            (dict(crop_x0=0, crop_y0=500000, crop_x1=500000, crop_y1=1000000, out_w=333), ["--crop", "0,0.5,0.5,1", "--width", "333"]),
            (dict(crop_ar_w=16, crop_ar_h=9, crop_ar_auto=0, out_h=250), ["--ratio", "16:9!", "--height", "250"]),
            (dict(crop_rot=4500, crop_x0=200000, crop_y0=250000, crop_x1=800000, crop_y1=750000, out_long=1200),
             ["--angle", "4.5", "--crop", "0.2,0.25,0.8,0.75", "--long", "1200"]),
            (dict(crop_rot=-12500), ["--angle", "-12.5"]),
            (dict(turn=1, crop_rot=3000, crop_ar_w=3, crop_ar_h=2, crop_ar_auto=1, out_w=700), ["--turn", "1", "--angle", "3", "--ratio", "3:2", "--width", "700"]),
            (dict(turn=3), ["--turn", "3"]),
        ]
        for photo in GEO_FILES:
            for ex, cli_opts in combos:
                c = geo_make(photo, cli_opts, quality=98, plain=False)
                if args.wasm:
                    w = wasm_make(os.path.join(args.samples, photo), 0, 0, {}, os.path.join(work, "w.jpg"), ex, legacy=False, quality=98)
                    check(bool(c) and bool(w) and sha(c) == sha(w), "%s geometry %s: browser version == command line" % (photo, " ".join(cli_opts)))
                else:
                    check(bool(c), "%s geometry %s: command line version ran" % (photo, " ".join(cli_opts)))
                if c:
                    hashes.append("%s  %s" % (sha(c), ("%s geometry %s" % (photo, " ".join(cli_opts))).replace(" ", "_")))
        if GEO_FILES and args.wasm:
            w = run([args.node, os.path.join(HERE, "test_wasm.js"), args.wasm, os.path.join(args.samples, GEO_FILES[0]), os.path.join(work, "wb.jpg"), "0", "0", "95", "100", "100", "out_long=20000"])
            check(w.returncode != 0 and b"ERROR" in w.stderr, "browser version: 20000 px is refused with a message")
except ImportError:
    print("(numpy / pillow not installed: crop and size tests skipped)")

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
            run([args.cli, "--quiet", "--film", "1", "--efekt", "0", "--ziarno", "0", "--quality", "100", "--usejpg", "1", "--size", size_name, "--out", d, path])
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
