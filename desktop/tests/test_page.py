#!/usr/bin/env python3
"""End to end test of the page in a real (headless) Chromium. Scenarios:
  main      pick a folder, remove one photo from the list, tick only some, choose a film, move sliders of every group (light,
            white balance, skin, colour mixer, grain), choose size M, develop, download the ZIP and compare every JPEG in it
            byte for byte with what the command line version makes; developed photos are unticked, a photo added later is the
            only one that is developed next
  wb        a picture with a warm colour cast: the pipette on the grey card makes the card neutral
  zoom      zoom in (key 1): the full size picture replaces the half size one; panning; the pipette sets the white balance
  bw        a black and white film: colour filter and toning (sepia) through the page == command line
  parallel  the same with 4 workers at once (size S)
  inline    the same when the browser has no Web Workers at all (size S)
  portrait  a CR2 marked "rotated" is shown upright in the preview; RAW+JPG pairs; the settings file of the camera (FILMLOOK.TXT)
  folder    "Zapisz w folderze" with a made-up folder object
  layout    the panels hide and show, the edge can be dragged; on a phone the panels slide over the picture
  curves    the white balance buttons (bulb, fluorescent ...), the colour of the grain, the tone curves (RGB, R, G, B): add, drag,
            delete and move points, buttons, a black and white film has the master curve only; developed == command line
  raw       CR3 and DNG files (the fake ones of make_samples.py): list, thumbnails, upright portraits, the same pictures as the command
            line version, a DNG without a preview is refused with a message

usage: test_page.py <FILMLOOK_DESKTOP.html> <filmlook_cli> <folder with photos> [screenshot folder] [--only name]
The folder holds *.jpg / *.cr2 / *.cr3 / *.dng (tests/make_samples.py makes them: it must contain synth_portrait.cr2 for "portrait").
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
html, cli, all_photos = [os.path.abspath(a) for a in argv[:3]]
photos = tempfile.mkdtemp(prefix="fdpages_")            # the scenarios other than "raw" work on the JPGs and CR2 files only
for _f in os.listdir(all_photos):
    if _f.lower().endswith((".jpg", ".jpeg", ".cr2")):
        shutil.copy(os.path.join(all_photos, _f), photos)
shots = os.path.abspath(argv[3]) if len(argv) > 3 else None
if shots:
    os.makedirs(shots, exist_ok=True)

FILM_NAME = "Fuji Velvia 50"            # in the tab "Slajd"
FILM_NUMBER = 11                        # its number for the command line (1 based)
fails = []

IDLE = ("document.querySelector('#pvImg').classList.contains('on') && !document.querySelector('#stage').classList.contains('busy')"
        " && !(document.querySelector('#hud').classList.contains('on') && document.querySelector('#hud').textContent.indexOf('szczegóły') >= 0)")


def annotate(msg):
    """On GitHub Actions a failure also becomes a visible annotation (readable without downloading the log)."""
    if os.environ.get("GITHUB_ACTIONS"):
        print("::error title=page test::" + msg.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")[:900], flush=True)


def check(cond, msg):
    print(("ok    " if cond else "FAIL  ") + msg, flush=True)
    if not cond:
        fails.append(msg)
        annotate(msg)


def shot(page, name):
    if shots:
        page.screenshot(path=os.path.join(shots, name + ".png"), full_page=False)


def open_page(browser, init=None, viewport=None):
    ctx = browser.new_context(viewport=viewport or {"width": 1400, "height": 1000}, accept_downloads=True)
    if init:
        ctx.add_init_script(init)
    page = ctx.new_page()
    page.errors = []
    page.on("pageerror", lambda e: page.errors.append(str(e)))
    page.on("console", lambda m: page.errors.append(m.text) if m.type == "error" else None)
    page.goto("file://" + html)
    page.wait_for_function("document.querySelector('#chipEngine').className.indexOf('ok') >= 0", timeout=30000)
    return page


def wait_idle(page, timeout=120000):
    page.wait_for_function(IDLE, timeout=timeout)
    page.wait_for_timeout(300)


def names_in_list(page):
    return page.eval_on_selector_all("#list li .nm", "els => els.map(e => e.textContent)")


def set_number(page, key, value):
    """types a value into the number box of a slider (the displayed number: for the exposure it is in stops)"""
    sel = "#sl-%s + input[type=number]" % key
    page.fill(sel, str(value))
    page.dispatch_event(sel, "change")


def open_section(page, sid):
    if "open" not in (page.get_attribute('.sec[data-id="%s"]' % sid, "class") or ""):
        page.click('.sec[data-id="%s"] .tgl' % sid)


def zip_of(page):
    with page.expect_download() as dl:
        page.click("#btnZip")
    return zipfile.ZipFile(dl.value.path())


def run_and_wait(page):
    page.click("#btnRun")
    page.wait_for_function("document.querySelector('#runInfo').textContent.indexOf('Gotowe') === 0", timeout=900000)


def cli_make(work, film, size_flag, flags, src):
    subprocess.run([cli, "--quiet", "--film", str(film), "--size", size_flag, "--quality", "95", "--out", work] + flags + [src],
                   check=True, capture_output=True)


def compare_zip(z, suffix, film, size_flag, flags):
    work = tempfile.mkdtemp()
    for name in z.namelist():
        base = name.rsplit(suffix, 1)[0]
        src = [f for f in os.listdir(photos) if f.rsplit(".", 1)[0] == base and f.lower().endswith((".cr2", ".jpg", ".jpeg"))]
        if not src:
            check(False, "source of %s found" % name)
            continue
        cli_make(work, film, size_flag, flags, os.path.join(photos, src[0]))
        check(z.read(name) == open(os.path.join(work, name), "rb").read(), "%s: page == command line" % name)
    shutil.rmtree(work, ignore_errors=True)


def choose_velvia(page):
    page.click("#tabs .tab:nth-child(3)")
    page.click("#films .fcard:has-text('Velvia 50')")
    check(FILM_NAME in page.inner_text("#fdesc"), "film chosen: " + FILM_NAME)


def scenario_main(browser):
    page = open_page(browser)
    check("filmów" in page.inner_text("#chipEngine"), "engine started: " + page.inner_text("#chipEngine"))
    check(page.is_visible("#pvMsg"), "the empty page invites to add photos")
    page.set_input_files("#inDir", photos)
    page.wait_for_selector("#list li")
    n = page.locator("#list li").count()
    wait_idle(page)
    check(n == 4, "folder: %d photos listed, the first one in the preview" % n)
    shot(page, "main_1_loaded")
    check("zaznaczone 4" in page.inner_text("#cnt"), "all new photos are ticked: " + page.inner_text("#cnt"))

    # remove one photo (the x of its row): the portrait CR2
    before = names_in_list(page)
    victim = [b for b in before if b.endswith("synth_portrait.cr2")][0]
    page.locator("#list li", has_text="synth_portrait.cr2").locator(".x").click()
    after = names_in_list(page)
    check(len(after) == 3 and victim not in after and [b for b in before if b != victim] == after, "a single photo is removed from the list: %s" % after)

    # tick only two of them (the small JPG and the landscape CR2)
    keep = [a for a in after if a.endswith("synth_small.jpg") or a.endswith("synth_landscape.cr2")]
    for i, nm in enumerate(after):
        if nm not in keep:
            page.locator("#list li").nth(i).locator(".ck").uncheck()
    check(len(keep) == 2 and "Wywołaj zaznaczone: 2 zdjęcia" in page.inner_text("#btnRun"), "only two ticked: " + page.inner_text("#btnRun"))
    page.click("#selAll")
    check("3 zdjęcia" in page.inner_text("#btnRun"), "\"wszystkie\" ticks all: " + page.inner_text("#btnRun"))
    page.click("#selNone")
    check(page.is_disabled("#btnRun"), "nothing ticked: the develop button is off")
    for nm in keep:
        page.locator("#list li", has_text=nm.rsplit("/", 1)[-1]).locator(".ck").check()

    # film and sliders of every group
    choose_velvia(page)
    set_number(page, "grain", 50)
    check(page.input_value("#sl-grain") == "50", "slider Ziarno = 50")
    set_number(page, "expo", 0.5)
    check(page.evaluate("FD.val('expo')") == 50, "exposure box is in stops (0.5 = 50): %s" % page.evaluate("FD.val('expo')"))
    open_section(page, "wb")
    set_number(page, "wb_temp", 20)
    open_section(page, "skin")
    set_number(page, "skin_sat", 15)
    open_section(page, "hsl")
    set_number(page, "hue_blue", 30)
    wait_idle(page)
    check(page.evaluate("document.querySelectorAll('#adj .sec.mod').length") >= 5, "sections with changes are marked")
    shot(page, "main_2_sliders")
    page.click("#btnSheet")
    page.wait_for_function("document.querySelectorAll('#films .fcard.has-th').length >= 4", timeout=180000)
    check(True, "film thumbnails appear")
    page.wait_for_function("document.querySelector('#btnSheet').textContent.indexOf('Pokaż') === 0", timeout=180000)
    # before / after
    page.click("#pvBefore")
    check(page.get_attribute("#pvBefore", "aria-pressed") == "true", "before/after switch")
    page.click("#pvAfter")
    # reset of one section and of the whole correction
    page.click('.sec[data-id="skin"] .rs')
    check(page.evaluate("FD.val('skin_sat')") == 0, "the reset of a section brings its sliders back")
    open_section(page, "skin")
    set_number(page, "skin_sat", 15)

    page.click("#sizes button[data-size='1']")
    flags = ["--ziarno", "50", "--expo", "50", "--wb_temp", "20", "--skin_sat", "15", "--hue_blue", "30"]
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for s in statuses if s.startswith("✓ gotowe"))
    check(ndone == 2 and all("błąd" not in s for s in statuses), "developed only the 2 ticked photos, no errors: %s" % statuses)
    ticks = page.eval_on_selector_all("#list li .ck", "els => els.map(e => e.checked)")
    check(not any(ticks), "developed photos are unticked afterwards: %s" % ticks)
    z = zip_of(page)
    check(z.testzip() is None and len(z.namelist()) == 2, "ZIP is valid and holds 2 files: %s" % z.namelist())
    compare_zip(z, "_Fuji_Velvia_50_M", FILM_NUMBER, "M", flags)
    shot(page, "main_3_done")

    # a photo added later is the only one that is developed next
    tmp = tempfile.mkdtemp()
    extra = os.path.join(tmp, "extra_photo.jpg")
    shutil.copy(os.path.join(photos, "synth_small.jpg"), extra)
    page.set_input_files("#inFiles", extra)
    page.wait_for_function("document.querySelectorAll('#list li').length === 4")
    check("Wywołaj zaznaczone: 1 zdjęcie" in page.inner_text("#btnRun"), "only the new photo is ticked: " + page.inner_text("#btnRun"))
    run_and_wait(page)
    z2 = zip_of(page)
    check(len(z2.namelist()) == 3 and any("extra_photo" in n for n in z2.namelist()), "the ZIP now holds the new one as well: %s" % z2.namelist())
    shutil.rmtree(tmp, ignore_errors=True)

    # keys: arrows move through the photos, Delete removes the one on the screen
    page.click("#list li:nth-child(1) .nm")
    page.keyboard.press("ArrowRight")
    page.wait_for_timeout(200)
    sel = page.eval_on_selector_all("#list li.sel .nm", "els => els.map(e => e.textContent)")
    check(len(sel) == 1 and sel[0] == names_in_list(page)[1], "arrow key selects the next photo: %s" % sel)
    page.click("#stage")
    page.keyboard.press("Delete")
    check(page.locator("#list li").count() == 3, "Delete key removes the selected photo")
    page.click("#btnClear")
    check(page.locator("#list li").count() == 0 and page.is_visible("#pvMsg"), "\"wyczyść\" empties the list and the page is empty again")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_wb(browser):
    """white balance: a made-up picture with a warm cast and a grey card; the pipette on the card must make the card neutral"""
    import numpy as np
    from PIL import Image
    tmp = tempfile.mkdtemp()
    a = np.zeros((800, 1200, 3), np.float32)
    a[:] = (110, 110, 110)
    a[:, 600:] = (150, 90, 60)
    a[100:300, 100:400] = (128, 128, 128)                  # the grey card
    a[500:700, 100:400] = (200, 200, 200)                  # a lighter grey patch
    a = np.clip(a * np.array([1.25, 1.0, 0.7], np.float32), 0, 255).astype(np.uint8)
    Image.fromarray(a).save(os.path.join(tmp, "cast.jpg"), quality=95)
    page = open_page(browser)
    page.set_input_files("#inFiles", os.path.join(tmp, "cast.jpg"))
    wait_idle(page)
    page.evaluate("FD.setVal('strength', 0)")                # the film out of the way: only the correction is seen
    wait_idle(page)
    patch = """([px, py]) => { const im = document.querySelector('#pvImg'), c = document.createElement('canvas'); c.width = im.naturalWidth; c.height = im.naturalHeight;
        const g = c.getContext('2d'); g.drawImage(im, 0, 0); const d = g.getImageData(Math.round(px * im.naturalWidth / 1200), Math.round(py * im.naturalHeight / 800), 8, 8).data;
        let r = 0, gg = 0, b = 0, n = 0; for (let i = 0; i < d.length; i += 4) { r += d[i]; gg += d[i + 1]; b += d[i + 2]; n++; } return [r / n, gg / n, b / n]; }"""
    spread = lambda v: max(v) - min(v)
    v0 = page.evaluate(patch, [250, 200])
    check(spread(v0) > 50, "the grey card has a cast before: %s" % [round(x) for x in v0])
    open_section(page, "wb")
    page.click("#btnPip")
    box = page.locator("#stage").bounding_box()
    pos = page.evaluate("[FD.V.tx + 250 * (FD.V.fw / 1200) * FD.V.s, FD.V.ty + 200 * (FD.V.fh / 800) * FD.V.s]")
    page.mouse.click(box["x"] + pos[0], box["y"] + pos[1])
    page.wait_for_function("document.querySelector('#pipNote').classList.contains('hidden')", timeout=10000)
    t, i = page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]")
    check(t < -50, "the pipette cools the warm picture: temperature %d, tint %d" % (t, i))
    wait_idle(page)
    page.wait_for_timeout(300)
    v1 = page.evaluate(patch, [250, 200])
    check(spread(v1) < 14, "and the grey card is neutral now: %s" % [round(x) for x in v1])
    v2 = page.evaluate(patch, [250, 600])
    check(spread(v2) < 22, "the lighter patch is nearly neutral too: %s" % [round(x) for x in v2])
    # \"Automatycznie\" on the same picture, then the reset of the section
    page.click("#btnAutoWB")
    page.wait_for_timeout(300)
    check(page.evaluate("FD.val('wb_temp')") < 0, "automatic white balance cools it as well")
    page.click('.sec[data-id="wb"] .rs')
    check(page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]") == [0, 0], "and the reset of the section brings 0 / 0 back")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_zoom(browser):
    page = open_page(browser)
    page.set_input_files("#inFiles", os.path.join(photos, "synth_5616x3744.jpg"))
    wait_idle(page)
    fit = page.evaluate("FD.V.s")
    check(0.1 < fit < 0.5, "starts fitted to the window (%.0f %%)" % (fit * 100))
    nat0 = page.evaluate("document.querySelector('#pvImg').naturalWidth")
    check(nat0 == 2808, "the half size picture is shown first (%d px wide)" % nat0)
    page.click("#stage")
    page.keyboard.press("1")
    check(abs(page.evaluate("FD.V.s") - 1) < 0.001 and page.inner_text("#zVal") == "100 %", "key 1 = 100 %")
    page.wait_for_function("document.querySelector('#pvImg').naturalWidth === 5616", timeout=60000)
    check(True, "the full size picture (5616 px) replaces the half size one when zoomed in")
    shot(page, "zoom_1_100")
    # panning by dragging
    t0 = page.evaluate("[FD.V.tx, FD.V.ty]")
    page.mouse.move(700, 500)
    page.mouse.down()
    page.mouse.move(600, 450, steps=5)
    page.mouse.up()
    t1 = page.evaluate("[FD.V.tx, FD.V.ty]")
    check(t1[0] < t0[0] - 50 and t1[1] < t0[1] - 20, "dragging moves the picture: %s -> %s" % (t0, t1))
    # wheel zoom around the pointer
    s0 = page.evaluate("FD.V.s")
    page.mouse.wheel(0, -300)
    page.wait_for_timeout(200)
    check(page.evaluate("FD.V.s") > s0 * 1.3, "mouse wheel zooms in")
    page.dblclick("#stage")
    check(abs(page.evaluate("FD.V.s") - 1) < 0.001, "double click zooms to 100 %")
    page.dblclick("#stage")
    check(page.evaluate("FD.V.fit"), "and the next double click goes back to the whole picture")
    page.click("#z100")
    check(abs(page.evaluate("FD.V.s") - 1) < 0.001, "button \"100 %\"")
    page.click("#zFit")
    check(page.evaluate("FD.V.fit"), "button \"Całe\"")
    # a slider change at 100 % makes the detail picture again
    page.keyboard.press("1")
    page.wait_for_function("document.querySelector('#pvImg').naturalWidth === 5616", timeout=60000)
    src0 = page.evaluate("document.querySelector('#pvImg').src")
    set_number(page, "expo", 1)
    page.wait_for_function("(s0) => document.querySelector('#pvImg').naturalWidth === 5616 && document.querySelector('#pvImg').src !== s0 && !document.querySelector('#stage').classList.contains('busy')", arg=src0, timeout=60000)
    check(True, "after a slider move the full size picture is made again")
    # the pipette: the picture is a colourful synthetic one, so only check that it sets the white balance and ends
    page.click("#zFit")
    open_section(page, "wb")
    page.click("#btnPip")
    check(page.is_visible("#pipNote"), "the pipette asks to click on something neutral")
    page.mouse.click(700, 500)
    page.wait_for_function("document.querySelector('#pipNote').classList.contains('hidden')", timeout=10000)
    t, i = page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]")
    check(t != 0 or i != 0, "the pipette set the white balance: temperature %d, tint %d" % (t, i))
    page.click("#btnAutoWB")
    page.wait_for_timeout(500)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_bw(browser):
    page = open_page(browser)
    page.set_input_files("#inFiles", os.path.join(photos, "synth_small.jpg"))
    wait_idle(page)
    check(page.locator('.sec[data-id="filter"]').count() == 0 and page.locator('.sec[data-id="tone"]').count() == 1, "a colour film has the toning (colour grading) but no colour filter")
    page.click("#tabs .tab:nth-child(2)")
    page.click("#films .fcard:has-text('Tri-X')")
    check(page.locator('.sec[data-id="filter"]').count() == 1 and page.locator('.sec[data-id="tone"]').count() == 1, "a black and white film has the colour filter and the toning")
    check(page.locator('.sec[data-id="color"] #sl-sat').count() == 0, "no saturation slider on a black and white film")
    open_section(page, "filter"); open_section(page, "tone"); open_section(page, "hsl")
    page.click("#filterChips .pill:has-text('Czerwony')")
    page.click("#toneChips .pill:has-text('Sepia')")
    set_number(page, "lum_green", -40)
    wait_idle(page)
    check(page.get_attribute("#toneChips .pill:has-text('Sepia')", "aria-pressed") == "true", "the toning button shows the chosen toning")
    shot(page, "bw_1")
    page.click("#sizes button[data-size='2']")
    run_and_wait(page)
    z = zip_of(page)
    flags = ["--filter", "3", "--ton_mode", "1", "--cg_sh_h", "30", "--cg_sh_s", "25", "--cg_mid_h", "40", "--cg_hi_h", "45", "--cg_hi_s", "20", "--lum_green", "-40"]
    compare_zip(z, "_Kodak_Tri_X_400_S", 7, "S", flags)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def develop_and_compare(page, size_value, size_flag, grain):
    """folder -> film -> slider -> size -> develop -> ZIP == command line"""
    page.set_input_files("#inDir", photos)
    page.wait_for_selector("#list li")
    wait_idle(page)
    choose_velvia(page)
    set_number(page, "grain", grain)
    check(page.input_value("#sl-grain") == str(grain), "slider Ziarno = %d" % grain)
    page.click("#sizes button[data-size='%d']" % size_value)
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for s in statuses if s.startswith("✓ gotowe"))
    check(ndone >= 1 and all("błąd" not in s for s in statuses), "developed %d, no errors: %s" % (ndone, statuses))
    z = zip_of(page)
    check(z.testzip() is None, "ZIP is valid (CRCs match)")
    check(len(z.namelist()) == ndone, "ZIP holds %d files: %s" % (ndone, z.namelist()))
    compare_zip(z, "_Fuji_Velvia_50" + ("", "_M", "_S")[size_value], FILM_NUMBER, size_flag, ["--ziarno", str(grain)])


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
    # the full size detail also works with one engine only
    page.locator("#list li .nm", has_text="synth_5616x3744").click()     # the big one (the order of the list depends on the system)
    wait_idle(page)
    page.click("#stage")
    page.keyboard.press("1")
    page.wait_for_function("document.querySelector('#pvImg').naturalWidth >= 3000", timeout=120000)
    check(True, "one engine only: the detail view works too")
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
    check("zaznaczone 2" in page.inner_text("#cnt"), "and it is not ticked: " + page.inner_text("#cnt"))
    page.click("#list li:has-text('synth_portrait.cr2')")
    page.wait_for_function(IDLE + " && document.querySelector('#pvImg').naturalWidth > 0", timeout=60000)
    page.wait_for_timeout(500)
    dims = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check(dims[1] > dims[0], "portrait CR2 stands upright in the preview: %s" % dims)
    fw, fh = page.evaluate("[FD.V.fw, FD.V.fh]")
    check(fh > fw, "and its full size dimensions on the screen are upright too: %s x %s" % (fw, fh))
    page.click("#list li:has-text('synth_landscape.cr2')")
    page.wait_for_timeout(500)
    wait_idle(page)
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
    run_and_wait(page)
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


def scenario_layout(browser):
    page = open_page(browser)
    page.set_input_files("#inFiles", [os.path.join(photos, "synth_small.jpg"), os.path.join(photos, "synth_landscape.cr2")])
    wait_idle(page)
    w0 = page.evaluate("document.querySelector('#left').getBoundingClientRect().width")
    check(w0 > 200, "the left panel is open (%d px)" % w0)
    page.click("#sbL")
    page.wait_for_timeout(500)
    check(page.evaluate("document.querySelector('#left').getBoundingClientRect().width") <= 2 and page.evaluate("document.querySelector('#app').classList.contains('lc')"), "a click on the edge hides the left panel")
    stage_w = page.evaluate("document.querySelector('#stage').clientWidth")
    check(stage_w > 1400 - 40 - 360, "the picture gets the room (%d px)" % stage_w)
    page.click("#sbL")
    check(page.evaluate("document.querySelector('#left').getBoundingClientRect().width") > 200, "and a second click shows it again")
    page.keyboard.press("]")
    check(page.evaluate("document.querySelector('#app').classList.contains('rc')"), "the key ] hides the right panel")
    page.keyboard.press("]")
    # drag the edge of the right panel to make it wider
    r0 = page.evaluate("document.querySelector('#right').getBoundingClientRect().width")
    box = page.locator("#splitR").bounding_box()
    page.mouse.move(box["x"] + box["width"] / 2, box["y"] + 100)
    page.mouse.down()
    page.mouse.move(box["x"] - 60, box["y"] + 100, steps=6)
    page.mouse.up()
    r1 = page.evaluate("document.querySelector('#right').getBoundingClientRect().width")
    check(r1 > r0 + 40, "dragging the edge widens the right panel: %d -> %d px" % (r0, r1))
    page.reload()
    page.wait_for_function("document.querySelector('#chipEngine').className.indexOf('ok') >= 0", timeout=30000)
    r2 = page.evaluate("document.querySelector('#right').getBoundingClientRect().width")
    check(abs(r2 - r1) < 3, "the width is remembered after a reload: %d px" % r2)
    # the sections open and close, and are remembered
    open_section(page, "wb")
    page.reload()
    page.wait_for_function("document.querySelector('#chipEngine').className.indexOf('ok') >= 0", timeout=30000)
    check("open" in page.get_attribute('.sec[data-id="wb"]', "class"), "an opened section stays open after a reload")
    page.click("#btnTheme")
    check(page.evaluate("document.documentElement.getAttribute('data-theme')") == "light", "the light theme can be switched on")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()

    # a phone: the panels start hidden, slide over the picture, and the list slides away when a photo is chosen
    page = open_page(browser, viewport={"width": 420, "height": 860})
    classes = page.get_attribute("#app", "class") or ""
    check("lc" in classes and "rc" in classes, "on a phone both panels start hidden: " + classes)
    page.set_input_files("#inFiles", [os.path.join(photos, "synth_small.jpg"), os.path.join(photos, "synth_landscape.cr2")])
    wait_idle(page)
    page.click("#btnL")
    page.wait_for_timeout(400)
    x = page.evaluate("document.querySelector('#left').getBoundingClientRect().left")
    check(x >= -1 and x < 5, "the left panel slides in")
    shot(page, "layout_phone_left")
    page.click("#list li:nth-child(2) .nm")
    page.wait_for_timeout(400)
    check(page.evaluate("document.querySelector('#left').getBoundingClientRect().right") <= 2, "choosing a photo closes the list")
    page.click("#btnR")
    page.wait_for_timeout(400)
    shot(page, "layout_phone_right")
    check(page.evaluate("document.querySelector('#right').getBoundingClientRect().right") <= 421 and page.evaluate("document.querySelector('#left').getBoundingClientRect().right") <= 2, "only one panel at a time")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_curves(browser):
    page = open_page(browser)
    page.set_input_files("#inFiles", os.path.join(photos, "synth_small.jpg"))
    page.wait_for_selector("#list li")
    wait_idle(page)

    # white balance as in a camera
    open_section(page, "wb")
    page.click("#wbChips .pill[data-id='tung']")
    check(page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]") == [-85, 0] and page.get_attribute("#wbChips .pill[data-id='tung']", "aria-pressed") == "true", "bulb: cooler, the button is lit")
    page.click("#wbChips .pill[data-id='fluo']")
    check(page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]") == [-35, 25], "fluorescent: cooler and less green")
    set_number(page, "wb_temp", 10)
    check(all(page.get_attribute("#wbChips .pill[data-id='%s']" % i, "aria-pressed") == "false" for i in ("asis", "cloud", "shade", "tung", "fluo", "flash")), "a hand-set value lights no button")
    page.click("#wbChips .pill[data-id='asis']")
    check(page.evaluate("[FD.val('wb_temp'), FD.val('wb_tint')]") == [0, 0] and page.get_attribute("#wbChips .pill[data-id='asis']", "aria-pressed") == "true", "\"Jak w aparacie\" brings it back")
    page.click("#wbChips .pill[data-id='tung']")                       # stays on for the development below (== command line)

    # the colour of the grain: a slider for colour films, none for black and white ones
    check(page.locator("#sl-gcolor").count() == 1 and page.input_value("#sl-gcolor") == "100", "colour film: the slider \"Kolor ziarna\" is there, at 100")
    set_number(page, "gcolor", 60)

    # the curves
    open_section(page, "curves")
    cv = page.locator("canvas.crvcv")
    check(cv.count() == 1 and page.locator('.sec[data-id="curves"] .tabs2 button').count() == 4, "curves: the graph and the tabs RGB R G B")
    def at(x, y):
        page.locator("canvas.crvcv").scroll_into_view_if_needed()       # the panel is long: the graph can be below the visible part
        b = page.locator("canvas.crvcv").bounding_box()
        return b["x"] + 6 + x / 255 * (b["width"] - 12), b["y"] + b["height"] - 6 - y / 255 * (b["height"] - 12)
    def crv():
        return page.evaluate("FD.S.scene.crv || null")
    x0, y0 = at(128, 128)
    page.mouse.move(x0, y0); page.mouse.down()
    x1, y1 = at(128, 92)
    page.mouse.move(x1, y1, steps=4); page.mouse.up()
    c = crv()
    check(c and len(c["m"]) == 3 and abs(c["m"][1][0] - 128) <= 2 and abs(c["m"][1][1] - 92) <= 2, "click + drag adds a point and moves it: %s" % (c and c["m"]))
    check("mod" in page.get_attribute('.sec[data-id="curves"]', "class"), "the section is marked as changed")
    shot(page, "curves_1")
    # the red curve: the end point at the top lowered
    page.click('.sec[data-id="curves"] .tabs2 button:nth-child(2)')
    cv = page.locator("canvas.crvcv")
    xa, ya = at(255, 255); xb, yb = at(255, 225)
    page.mouse.move(xa, ya); page.mouse.down(); page.mouse.move(xb, yb, steps=3); page.mouse.up()
    c = crv()
    check(c and "r" in c and c["r"][-1][0] == 255 and 220 <= c["r"][-1][1] <= 230, "red: the end point is moved: %s" % (c and c.get("r")))
    check(page.get_attribute('.sec[data-id="curves"] .tabs2 button:nth-child(2)', "class") == "mod" or "mod" in page.get_attribute('.sec[data-id="curves"] .tabs2 button:nth-child(2)', "class"), "the tab of a changed channel is marked")
    # develop with these curves: page == command line
    page.click("#sizes button[data-size='2']")
    run_and_wait(page)
    z = zip_of(page)
    c = crv()
    flags = ["--wb_temp", "-85", "--gcolor", "60"]
    for k, key in enumerate("mrgb"):
        if key in c:
            pts = c[key]
            flags += ["--crv%d_n" % k, str(len(pts))]
            for j, (px_, py_) in enumerate(pts):
                flags += ["--crv%d_x%d" % (k, j), str(px_), "--crv%d_y%d" % (k, j), str(py_)]
    compare_zip(z, "_Kodak_Portra_400_S", 1, "S", flags)
    # the picture really got darker in the middle: compare with the same without the master curve
    # keys and the double click
    page.click('.sec[data-id="curves"] .tabs2 button:nth-child(1)')
    cv = page.locator("canvas.crvcv")
    xm, ym = at(128, 92)
    page.mouse.click(xm, ym)                                                 # selects the point
    page.keyboard.press("ArrowUp")
    page.keyboard.press("Shift+ArrowRight")
    c = crv()
    check(c["m"][1][1] == 93 and c["m"][1][0] == 138, "arrow keys move the selected point: %s" % c["m"])
    check(page.evaluate("document.activeElement.className") == "crvcv" and page.locator("#list li").count() == 1, "the keys do not leave the graph (the photo is still there)")
    xm, ym = at(138, 93)
    page.mouse.dblclick(xm, ym)
    c = crv()
    check(not c or "m" not in c, "double click on a point removes it (back to the straight line): %s" % (c and c.get("m")))
    # buttons
    page.click("#crvChips .pill[data-id='hard']")
    check(crv()["m"] == [[0, 0], [64, 40], [192, 220], [255, 255]] and page.get_attribute("#crvChips .pill[data-id='hard']", "aria-pressed") == "true", "the button \"Mocny kontrast\" draws an S curve")
    page.click('.sec[data-id="curves"] .rs')
    check(crv() is None, "the reset of the section clears every curve")
    page.click("#crvReset")
    # a black and white film has the master curve only
    page.click("#tabs .tab:nth-child(2)")
    page.click("#films .fcard:has-text('Tri-X')")
    open_section(page, "curves")
    check(page.locator('.sec[data-id="curves"] .tabs2').count() == 0 and page.locator("#sl-gcolor").count() == 0, "black and white film: no channel tabs, no colour grain")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_raw(browser):
    """CR3 and DNG: the JPEG inside is found by the page (thumbnails, preview, upright portraits) and by the engine (the same
    pictures as the command line version); a DNG with no big preview gives a clear message and no picture"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import make_samples as ms
    tmp = tempfile.mkdtemp()
    names = {"synth_landscape.cr3": "synth_landscape.cr3", "synth_portrait.cr3": "synth_portrait.cr3", "synth_prvw_only.cr3": "synth_prvw_only.cr3",
             "synth_landscape.dng": "dng_landscape.dng", "synth_portrait.dng": "dng_portrait.dng"}      # (other stems: the output names stay apart)
    for n, to in names.items():
        shutil.copy(os.path.join(all_photos, n), os.path.join(tmp, to))
    tj = os.path.join(tmp, "_t.jpg")
    ms.save_jpeg(ms.picture(800, 600, 3), tj, 90)
    ms.fake_dng(os.path.join(tmp, "dng_nopreview.dng"), tj, 800, 600, 1, tj, be=True, with_preview=False)
    os.remove(tj)
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_selector("#list li")
    page.wait_for_function("document.querySelectorAll('#list li').length === 6", timeout=20000)
    metas = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    check(sum(1 for m in metas if m.endswith("CR3")) == 3 and sum(1 for m in metas if m.endswith("DNG")) == 3, "CR3 and DNG are listed with their kind: %s" % metas)
    # thumbnails of the five that have a preview
    page.wait_for_function("document.querySelectorAll('#list li .th img').length === 5", timeout=60000)
    check(True, "thumbnails of the five photos with a preview")
    for nm, upright in (("synth_portrait.cr3", True), ("dng_portrait.dng", True), ("synth_landscape.cr3", False), ("dng_landscape.dng", False), ("synth_prvw_only.cr3", False)):
        page.click("#list li:has-text('%s')" % nm)
        page.wait_for_timeout(400)
        page.wait_for_function(IDLE + " && document.querySelector('#pvImg').naturalWidth > 0", timeout=60000)
        wait_idle(page)
        dims = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
        check((dims[1] > dims[0]) == upright, "%s is %s in the preview: %s" % (nm, "upright" if upright else "landscape", dims))
    fw, fh = page.evaluate("[FD.V.fw, FD.V.fh]")
    check(fw == 1620 and fh == 1080, "the CR3 without a JPEG track shows its 1620 x 1080 preview: %s x %s" % (fw, fh))
    page.click("#list li:has-text('dng_nopreview.dng')")
    page.wait_for_function("document.querySelector('#pvMsg') && !document.querySelector('#pvMsg').classList.contains('hidden') && document.querySelector('#pvMsg').textContent.indexOf('podgl') >= 0", timeout=20000)
    check(True, "a DNG with no preview: the page says so: " + page.inner_text("#pvMsg")[:90])
    shot(page, "raw_1")
    # develop all of them (size S): the five good ones == the command line version, the sixth is reported as an error
    page.click("#sizes button[data-size='2']")
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for m in statuses if m.startswith("✓ gotowe"))
    nerr = sum(1 for m in statuses if m.startswith("błąd"))
    check(ndone == 5 and nerr == 1 and any("podglądu" in m for m in statuses if m.startswith("błąd")), "5 developed, 1 refused with an error about the missing preview: %s" % statuses)
    z = zip_of(page)
    check(z.testzip() is None and len(z.namelist()) == 5, "ZIP holds the 5 pictures: %s" % z.namelist())
    work = tempfile.mkdtemp()
    for name in z.namelist():
        src = os.path.join(tmp, [f for f in os.listdir(tmp) if f.rsplit(".", 1)[0] == name.rsplit("_Kodak_Portra_400_S", 1)[0]][0])
        cli_make(work, 1, "S", [], src)
        check(z.read(name) == open(os.path.join(work, name), "rb").read(), "%s: page == command line" % name)
    shutil.rmtree(work, ignore_errors=True)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


SCENARIOS = [("main", scenario_main), ("zoom", scenario_zoom), ("wb", scenario_wb), ("bw", scenario_bw), ("parallel", scenario_parallel),
             ("inline", scenario_inline), ("portrait", scenario_portrait), ("folder", scenario_folder), ("layout", scenario_layout), ("curves", scenario_curves), ("raw", scenario_raw)]
with sync_playwright() as p:
    browser = p.chromium.launch()
    for name, fn in SCENARIOS:
        if only and only != name:
            continue
        print("\n== " + name, flush=True)
        if name == "raw" and not os.path.exists(os.path.join(all_photos, "synth_portrait.dng")):
            print("(skipped: no synth_portrait.dng etc. in the folder: use tests/make_samples.py)")
            continue
        if name in ("portrait", "folder", "layout") and not os.path.exists(os.path.join(photos, "synth_portrait.cr2")):
            print("(skipped: no synth_portrait.cr2 etc. in the folder: use tests/make_samples.py)")
            continue
        try:
            fn(browser)
        except Exception as e:                      # one broken scenario must not hide the others
            msg = "scenario %s stopped: %s: %s" % (name, type(e).__name__, str(e).strip().splitlines()[0] if str(e).strip() else "")
            print("FAIL  " + msg, flush=True)
            fails.append(msg)
            annotate(msg + " | " + " ".join(str(e).split())[:600])
    browser.close()

print("\n%d problem(s)" % len(fails))
sys.exit(1 if fails else 0)
