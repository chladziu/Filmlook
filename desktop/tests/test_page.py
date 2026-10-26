#!/usr/bin/env python3
"""End to end test of the page in a real (headless) Chromium. Scenarios:
  main      pick a folder, choose a film, move a slider, show all films, choose size M, develop, download the ZIP, and compare
            every JPEG in it byte for byte with what the command line version makes
  parallel  the same with 4 workers at once (size S)
  inline    the same when the browser has no Web Workers at all (size S)
  portrait  a CR2 marked "rotated" is shown upright in the preview; RAW+JPG pairs; the settings file of the camera (FILMLOOK.TXT)

usage: test_page.py <FILMLOOK_DESKTOP.html> <filmlook_cli> <folder with photos> [screenshot folder] [--only name]
The folder holds *.jpg / *.cr2 (tests/make_samples.py makes them: it must contain synth_portrait.cr2 for "portrait").
needs: pip install playwright; playwright install chromium"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

from playwright.sync_api import sync_playwright

for _s in (sys.stdout, sys.stderr):                 # utf-8 even when the system's code page cannot print Polish letters
    try:
        _s.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

argv = [a for a in sys.argv[1:]]
only = None
if "--only" in argv:
    i = argv.index("--only")
    only = argv[i + 1]
    del argv[i:i + 2]
html, cli, photos = [os.path.abspath(a) for a in argv[:3]]
shots = os.path.abspath(argv[3]) if len(argv) > 3 else None
if shots:
    os.makedirs(shots, exist_ok=True)

FILM_NAME = "Fuji Velvia 50"            # in the tab "Slajd"
FILM_NUMBER = 11                        # its number for the command line (1 based)
fails = []


def check(cond, msg):
    print(("ok    " if cond else "FAIL  ") + msg, flush=True)
    if not cond:
        fails.append(msg)


def shot(page, name):
    if shots:
        page.screenshot(path=os.path.join(shots, name + ".png"), full_page=True)


def open_page(browser, init=None):
    ctx = browser.new_context(viewport={"width": 1400, "height": 1000}, accept_downloads=True)
    if init:
        ctx.add_init_script(init)
    page = ctx.new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.on("console", lambda m: page.errors.append(m.text) if m.type == "error" else None)
    page.goto("file://" + html)
    page.wait_for_function("document.querySelector('#chipEngine').className.indexOf('ok') >= 0", timeout=30000)
    return page


def develop_and_compare(page, size_value, size_flag, grain, tab_label_index=3):
    """folder -> film -> slider -> size -> develop -> ZIP == command line"""
    page.set_input_files("#inDir", photos)
    page.wait_for_selector("#list li")
    page.wait_for_function("!document.querySelector('#pvImg').classList.contains('hidden')", timeout=120000)
    check(True, "photos listed (%d) and the preview shown" % page.locator("#list li").count())
    page.click("#tabs .tab:nth-child(%d)" % tab_label_index)
    page.click("#films .fcard:has-text('%s')" % FILM_NAME.split(" ", 1)[1])
    check(FILM_NAME in page.inner_text("#fdesc"), "film chosen: " + FILM_NAME)
    page.fill("#sl-grain ~ input[type=number]", str(grain))
    page.dispatch_event("#sl-grain ~ input[type=number]", "change")
    check(page.input_value("#sl-grain") == str(grain), "slider Ziarno = %d" % grain)
    page.click("label.opt:has(input[value='%d'])" % size_value)
    page.click("#btnRun")
    page.wait_for_function("document.querySelector('#runInfo').textContent.indexOf('Gotowe') === 0", timeout=900000)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for s in statuses if s.startswith("gotowe"))
    check(ndone >= 1 and all("błąd" not in s for s in statuses), "developed %d, no errors: %s" % (ndone, statuses))
    with page.expect_download() as dl:
        page.click("#btnZip")
    z = zipfile.ZipFile(dl.value.path())
    check(z.testzip() is None, "ZIP is valid (CRCs match)")
    names = z.namelist()
    check(len(names) == ndone, "ZIP holds %d files: %s" % (len(names), names))
    suffix = "_Fuji_Velvia_50" + ("", "_M", "_S")[size_value]
    work = tempfile.mkdtemp()
    for name in names:
        base = name.rsplit(suffix, 1)[0]
        src = [f for f in os.listdir(photos) if f.rsplit(".", 1)[0] == base and f.lower().endswith((".cr2", ".jpg", ".jpeg"))]
        if not src:
            check(False, "source of %s found" % name)
            continue
        subprocess.run([cli, "--quiet", "--film", str(FILM_NUMBER), "--size", size_flag, "--ziarno", str(grain), "--quality", "95",
                        "--out", work, os.path.join(photos, src[0])], check=True, capture_output=True)
        check(z.read(name) == open(os.path.join(work, name), "rb").read(), "%s: page == command line" % name)
    shutil.rmtree(work, ignore_errors=True)


def scenario_main(browser):
    page = open_page(browser)
    check("filmów" in page.inner_text("#chipEngine"), "engine started: " + page.inner_text("#chipEngine"))
    page.set_input_files("#inDir", photos)
    page.wait_for_function("!document.querySelector('#pvImg').classList.contains('hidden')", timeout=120000)
    shot(page, "main_1_loaded")
    page.click("#tabs .tab:nth-child(3)")
    page.click("#films .fcard:has-text('Velvia 50')")
    page.fill("#sl-grain ~ input[type=number]", "50")
    page.dispatch_event("#sl-grain ~ input[type=number]", "change")
    page.click("#btnSheet")
    page.wait_for_function("document.querySelectorAll('#films .fcard.has-th').length >= 5", timeout=180000)
    check(True, "film thumbnails appear")
    shot(page, "main_2_sheet")
    page.wait_for_function("document.querySelector('#btnSheet').textContent.indexOf('Pokaż') === 0", timeout=180000)
    page.click("#pvBefore")
    check(page.get_attribute("#pvBefore", "aria-pressed") == "true", "before/after switch")
    page.click("#pvAfter")
    page.click("#btnReset")
    check(page.input_value("#sl-grain") == "100", "reset brings the sliders back")
    page.context.close()
    page = open_page(browser)
    develop_and_compare(page, 1, "M", 50)
    shot(page, "main_3_done")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_parallel(browser):
    page = open_page(browser, "Object.defineProperty(navigator, 'hardwareConcurrency', {get: () => 8});")
    chip = page.inner_text("#chipEngine")
    check("równolegle: 4" in chip, "4 workers: " + chip)
    develop_and_compare(page, 2, "S", 30)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_inline(browser):
    page = open_page(browser, "window.Worker = undefined;")
    chip = page.inner_text("#chipEngine")
    check("pojedynczy" in chip, "no workers, one engine on the page: " + chip)
    develop_and_compare(page, 2, "S", 30)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_portrait(browser):
    tmp = tempfile.mkdtemp()
    shutil.copy(os.path.join(photos, "synth_portrait.cr2"), tmp)
    shutil.copy(os.path.join(photos, "synth_landscape.cr2"), tmp)
    shutil.copy(os.path.join(photos, "synth_small.jpg"), os.path.join(tmp, "synth_landscape.jpg"))     # the JPG of a RAW+JPG pair
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_selector("#list li")
    metas = page.eval_on_selector_all("#list li", "els => els.map(e => e.textContent)")
    check(any("pominięte" in m and "synth_landscape.jpg" in m for m in metas), "the JPG of a RAW+JPG pair is left out: %s" % metas)
    page.click("#list li:has-text('synth_portrait.cr2')")
    page.wait_for_function("!document.querySelector('#pvImg').classList.contains('hidden') && document.querySelector('#pvImg').naturalWidth > 0 "
                           "&& !document.querySelector('#frame').classList.contains('busy')", timeout=60000)
    dims = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check(dims[1] > dims[0], "portrait CR2 stands upright in the preview: %s" % dims)
    page.click("#list li:has-text('synth_landscape.cr2')")
    page.wait_for_timeout(500)
    page.wait_for_function("!document.querySelector('#frame').classList.contains('busy')", timeout=60000)
    page.wait_for_timeout(800)
    dims = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check(dims[0] > dims[1], "landscape CR2 stays landscape: %s" % dims)
    shot(page, "portrait_1")

    # the settings file of the camera
    cfg = os.path.join(tmp, "FILMLOOK.TXT")
    with open(cfg, "w") as f:
        f.write("film=Fuji Velvia 100\npush=0\njpg=2\ntheme=1\nlang=0\nmod=Fuji Velvia 100;100,80,10,0,5,-10,20,0,15,10\n")
    page.set_input_files("#inCfg", cfg)
    page.wait_for_function("document.querySelector('#fdesc b').textContent === 'Fuji Velvia 100'", timeout=10000)
    got = page.evaluate("['grain','contrast','fade','warm','shadow','high','sat','vig'].map(k => document.querySelector('#sl-' + k).value).join(',')")
    check(got == "80,10,5,-10,20,0,15,10", "settings of the card loaded: " + got)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_folder(browser):
    """"Zapisz w folderze" (the File System Access API of Chrome / Edge) with a made-up folder object: every finished photo is
    written at once and equals what the command line version makes"""
    import zlib
    mock = ("window.__written = {}; window.showDirectoryPicker = async () => ({ name: 'MOCK', getFileHandle: async (n) => ({"
            " createWritable: async () => { const parts = []; return { write: async (b) => { parts.push(b); },"
            " close: async () => { window.__written[n] = new Blob(parts); } }; } }) });")
    page = open_page(browser, mock)
    check(page.is_visible("#btnFolder"), "the folder button is there when the browser can write to folders")
    page.set_input_files("#inFiles", [os.path.join(photos, "synth_small.jpg"), os.path.join(photos, "synth_landscape.cr2")])
    page.wait_for_selector("#list li")
    page.click("#btnFolder")
    page.wait_for_function("document.querySelector('#folderInfo').textContent.indexOf('MOCK') >= 0", timeout=10000)
    page.click("#btnRun")
    page.wait_for_function("document.querySelector('#runInfo').textContent.indexOf('Gotowe') === 0", timeout=300000)
    got = page.evaluate("(async () => { const o = {}; for (const [k, b] of Object.entries(window.__written)) o[k] = [b.size, fdCrc32(new Uint8Array(await b.arrayBuffer()))]; return o; })()")
    check(sorted(got) == ["synth_landscape_Kodak_Portra_400.jpg", "synth_small_Kodak_Portra_400.jpg"], "both photos written to the folder: %s" % sorted(got))
    work = tempfile.mkdtemp()
    for src in ("synth_small.jpg", "synth_landscape.cr2"):
        subprocess.run([cli, "--quiet", "--film", "1", "--quality", "95", "--out", work, os.path.join(photos, src)], check=True, capture_output=True)
    for name, (size, crc) in got.items():
        want = open(os.path.join(work, name), "rb").read()
        check(size == len(want) and crc == zlib.crc32(want), "%s in the folder == command line" % name)
    shutil.rmtree(work, ignore_errors=True)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


SCENARIOS = [("main", scenario_main), ("parallel", scenario_parallel), ("inline", scenario_inline), ("portrait", scenario_portrait),
             ("folder", scenario_folder)]
with sync_playwright() as p:
    browser = p.chromium.launch()
    for name, fn in SCENARIOS:
        if only and only != name:
            continue
        print("\n== " + name, flush=True)
        if name in ("portrait", "folder") and not os.path.exists(os.path.join(photos, "synth_portrait.cr2")):
            print("(skipped: no synth_portrait.cr2 etc. in the folder: use tests/make_samples.py)")
            continue
        fn(browser)
    browser.close()

print("\n%d problem(s)" % len(fails))
sys.exit(1 if fails else 0)
