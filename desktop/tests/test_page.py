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
  sensor    RAW files with real sensor data (CR2 of the 5D Mark II, DNG: synth_raw_*): the picture is developed from the sensor data
            ("z RAW"), the switches "JPG z pliku" / "wzorzec z JPG" / "najwyższa jakość" work, developed == command line, a CR3 says
            what to do (DNG)

  rotate    straightening in the crop tool (angle: number, slider, a drag outside the frame, the line along the horizon), quarter turns, the
            frame stays inside the turned picture, Esc, removal; developed == command line (--angle --turn --crop)
  groups    the right panel: the strip of icons with one group each, the group "Zapis" opened by the button at the top, Tab hides the panels
  grain     the preview is counted like the file, 100 % = a pixel of the finished picture, the full size picture == the developed file
  crop      the crop tool (frame, handles, lock of the ratio, the list of ratios, "Dla wszystkich", Esc) and the free size of the result (also
            bigger than the photo); developed == command line

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
    if _f.lower().endswith((".jpg", ".jpeg", ".cr2")) and not _f.startswith("synth_raw_"):
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
    wrap_reveal(page)
    return page


def wrap_reveal(page):
    """The settings of the right panel are in groups (one icon each) and only one group is shown: before a click or a typing the group
    that holds the element is opened (the page's FD.reveal), as a person would do with the icons."""
    for name in ("click", "fill", "select_option", "check", "uncheck", "hover", "dblclick"):
        def make(orig):
            def wrapped(selector, *a, **k):
                if isinstance(selector, str) and not selector.startswith(("text=", "xpath=", "//")):
                    try:
                        page.evaluate("s => window.FD && FD.reveal(s)", selector)
                    except Exception:
                        pass
                return orig(selector, *a, **k)
            return wrapped
        setattr(page, name, make(getattr(page, name)))


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


PAGE_QUALITY = "98"                                  # the quality the page starts with


# the page has no fixed sizes any more (L / M / S): "any size" is a long side in pixels. These are the sizes the old tests used (the 5D's M and S).
SIZE_FLAGS = {"L": ["--size", "L"], "M": ["--long", "3861"], "S": ["--long", "2784"]}
SIZE_SUF = {"L": "", "M": "_3861px", "S": "_2784px"}
SIZE_PX = {"L": None, "M": 3861, "S": 2784}


def choose_size(page, code):
    """sets "Rozmiar" of the page: L = full size, M / S = a long side of 3861 / 2784 px"""
    if SIZE_PX[code] is None:
        page.select_option("#outMode", "full")
    else:
        page.select_option("#outMode", "long")
        page.fill("#outPx", str(SIZE_PX[code]))
        page.dispatch_event("#outPx", "change")


def cli_make(work, film, size_flag, flags, src):
    subprocess.run([cli, "--quiet", "--film", str(film)] + SIZE_FLAGS[size_flag] + ["--quality", PAGE_QUALITY, "--out", work] + flags + [src],
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
    check(int(page.get_attribute("#chipEngine", "data-films") or 0) >= 15 and page.inner_text("#chipEngine") == "" and not page.is_visible("#chipEngine"),
          "engine started and the top bar says nothing about it (no \"silnik gotowy\" chip): films=%s" % page.get_attribute("#chipEngine", "data-films"))
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

    choose_size(page, "M")
    flags = ["--ziarno", "50", "--expo", "50", "--wb_temp", "20", "--skin_sat", "15", "--hue_blue", "30"]
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for s in statuses if s.startswith("✓ gotowe"))
    check(ndone == 2 and all("błąd" not in s for s in statuses), "developed only the 2 ticked photos, no errors: %s" % statuses)
    ticks = page.eval_on_selector_all("#list li .ck", "els => els.map(e => e.checked)")
    check(not any(ticks), "developed photos are unticked afterwards: %s" % ticks)
    z = zip_of(page)
    check(z.testzip() is None and len(z.namelist()) == 2, "ZIP is valid and holds 2 files: %s" % z.namelist())
    compare_zip(z, "_Fuji_Velvia_50" + SIZE_SUF["M"], FILM_NUMBER, "M", flags)
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
    choose_size(page, "S")
    run_and_wait(page)
    z = zip_of(page)
    flags = ["--filter", "3", "--ton_mode", "1", "--cg_sh_h", "30", "--cg_sh_s", "25", "--cg_mid_h", "40", "--cg_hi_h", "45", "--cg_hi_s", "20", "--lum_green", "-40"]
    compare_zip(z, "_Kodak_Tri_X_400" + SIZE_SUF["S"], 7, "S", flags)
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
    choose_size(page, "LMS"[size_value])
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for s in statuses if s.startswith("✓ gotowe"))
    check(ndone >= 1 and all("błąd" not in s for s in statuses), "developed %d, no errors: %s" % (ndone, statuses))
    z = zip_of(page)
    check(z.testzip() is None, "ZIP is valid (CRCs match)")
    check(len(z.namelist()) == ndone, "ZIP holds %d files: %s" % (ndone, z.namelist()))
    compare_zip(z, "_Fuji_Velvia_50" + ("", SIZE_SUF["M"], SIZE_SUF["S"])[size_value], FILM_NUMBER, size_flag, ["--ziarno", str(grain)])


def scenario_parallel(browser):
    page = open_page(browser, "Object.defineProperty(navigator, 'hardwareConcurrency', {get: () => 8});")
    check(page.get_attribute("#chipEngine", "data-lanes") == "4" and page.get_attribute("#chipEngine", "data-inline") == "0", "4 workers: lanes=%s" % page.get_attribute("#chipEngine", "data-lanes"))
    develop_and_compare(page, 2, "S", 30)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_inline(browser):
    page = open_page(browser, "window.Worker = undefined;")
    check(page.get_attribute("#chipEngine", "data-inline") == "1", "no workers, one engine on the page")
    develop_and_compare(page, 2, "S", 30)
    # the full size detail also works with one engine only
    page.locator("#list li .nm", has_text="synth_5616x3744").click()     # the big one (the order of the list depends on the system)
    wait_idle(page)
    page.click("#stage")
    page.keyboard.press("1")
    page.wait_for_function("document.querySelector('#pvImg').naturalWidth >= 2700", timeout=120000)     # the detail is the finished picture: 2784 px (size S)
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
    page.click('#rail .rb[data-g="export"]')
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
        subprocess.run([cli, "--quiet", "--film", "1", "--quality", PAGE_QUALITY, "--out", work, os.path.join(photos, src)], check=True, capture_output=True)
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
    choose_size(page, "S")
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
    compare_zip(z, "_Kodak_Portra_400" + SIZE_SUF["S"], 1, "S", flags)
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
    choose_size(page, "S")
    run_and_wait(page)
    statuses = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
    ndone = sum(1 for m in statuses if m.startswith("✓ gotowe"))
    nerr = sum(1 for m in statuses if m.startswith("błąd"))
    check(ndone == 5 and nerr == 1 and any("podglądu" in m for m in statuses if m.startswith("błąd")), "5 developed, 1 refused with an error about the missing preview: %s" % statuses)
    z = zip_of(page)
    check(z.testzip() is None and len(z.namelist()) == 5, "ZIP holds the 5 pictures: %s" % z.namelist())
    work = tempfile.mkdtemp()
    for name in z.namelist():
        src = os.path.join(tmp, [f for f in os.listdir(tmp) if f.rsplit(".", 1)[0] == name.rsplit("_Kodak_Portra_400" + SIZE_SUF["S"], 1)[0]][0])
        cli_make(work, 1, "S", [], src)
        check(z.read(name) == open(os.path.join(work, name), "rb").read(), "%s: page == command line" % name)
    shutil.rmtree(work, ignore_errors=True)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_sensor(browser):
    """real sensor data: the page develops a CR2 / DNG from the RAW, the three switches work, developed == command line"""
    import re
    tmp = tempfile.mkdtemp()
    names = {"synth_raw_5d2_landscape.cr2": "sensor_a.cr2", "synth_raw_5d2_portrait.cr2": "sensor_b.cr2", "synth_raw_strips.dng": "sensor_c.dng",
             "synth_raw_tiles.dng": "sensor_d.dng", "synth_raw_linear.dng": "sensor_e.dng",
             "synth_raw_cr3_lossless.cr3": "sensor_g.cr3", "synth_raw_cr3_portrait.cr3": "sensor_h.cr3"}          # (CR3 of the EOS RP: crxgen.py)
    for n, to in names.items():
        shutil.copy(os.path.join(all_photos, n), os.path.join(tmp, to))
    cr3 = os.path.join(all_photos, "synth_landscape.cr3")
    if os.path.exists(cr3):
        shutil.copy(cr3, os.path.join(tmp, "sensor_f.cr3"))
    nfiles = len(os.listdir(tmp))
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_function("document.querySelectorAll('#list li').length === %d" % nfiles, timeout=20000)
    wait_idle(page)
    cap = page.inner_text("#pvCap")
    check("z RAW" in cap, "the picture comes from the sensor data: " + cap)
    info = page.inner_text("#srcInfo")
    check("surowe dane" in info and "RAW" in info, "the source section says it: " + info[:80])
    check(page.get_attribute('#srcSeg button[data-v="0"]', "aria-pressed") == "true", "RAW is the chosen source")
    check(page.is_checked("#chkRef") and not page.is_disabled("#chkRef"), "the pattern of the camera's JPEG is on, and can be switched")
    pv0 = page.evaluate("FD.S.pv.afterBlob.size")
    shot(page, "sensor_1_raw")
    # the pattern off: the tone changes, the source stays the sensor data
    page.uncheck("#chkRef")
    wait_idle(page)
    pv1 = page.evaluate("FD.S.pv.afterBlob.size")
    check(page.evaluate("FD.val('rawref')") == 0 and pv1 != pv0 and "z RAW" in page.inner_text("#pvCap"), "pattern off: another tone, still from the sensor data (%d -> %d bytes)" % (pv0, pv1))
    page.check("#chkRef")
    wait_idle(page)
    # the JPEG of the file instead
    page.click('#srcSeg button[data-v="1"]')
    wait_idle(page)
    check("z JPG" in page.inner_text("#pvCap") and page.is_disabled("#chkRef"), "JPG z pliku: the caption says so, the pattern switch is off: " + page.inner_text("#pvCap")[:60])
    check(page.evaluate("document.querySelector('#adj .sec[data-id=\"source\"]').classList.contains('mod')"), "the section is marked as changed")
    shot(page, "sensor_2_jpg")
    page.click('#srcSeg button[data-v="0"]')
    wait_idle(page)
    check("z RAW" in page.inner_text("#pvCap"), "back to RAW")
    # a CR3 that holds no sensor data the program can read (the fake one has only JPEGs): the page says what to do
    if os.path.exists(cr3):
        page.click("#list li:has-text('sensor_f.cr3')")
        wait_idle(page)
        check("nie czyta" in page.inner_text("#srcInfo") and "DNG" in page.inner_text("#srcInfo") and "z JPG" in page.inner_text("#pvCap") and page.is_disabled('#srcSeg button[data-v="0"]'),
              "CR3 without readable sensor data: the page says so and works on the JPEG: " + page.inner_text("#srcInfo")[:70])
        page.click("#list li:has-text('sensor_a.cr2')")
        wait_idle(page)
        check("z RAW" in page.inner_text("#pvCap"), "back on the CR2: from the sensor data again")
    # a CR3 of the EOS RP: developed from the sensor data like a CR2 (lossless in tiles / C-RAW)
    page.click("#list li:has-text('sensor_g.cr3')")
    wait_idle(page)
    check("z RAW" in page.inner_text("#pvCap") and "surowe dane" in page.inner_text("#srcInfo"), "CR3 (lossless): from the sensor data: " + page.inner_text("#pvCap")[:60] + " | " + page.inner_text("#srcInfo")[:60])
    check(page.get_attribute('#srcSeg button[data-v="0"]', "aria-pressed") == "true" and not page.is_disabled('#srcSeg button[data-v="1"]'), "CR3: RAW is the chosen source, and JPG z pliku can be chosen")
    page.click("#list li:has-text('sensor_h.cr3')")
    wait_idle(page)
    dims_h = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check("z RAW" in page.inner_text("#pvCap") and dims_h[1] > dims_h[0], "the rotated CR3 (C-RAW) is from the sensor data and upright: %s" % dims_h)
    shot(page, "sensor_1b_cr3")
    page.click("#list li:has-text('sensor_a.cr2')")
    wait_idle(page)
    # portraits stand upright
    page.click("#list li:has-text('sensor_b.cr2')")
    wait_idle(page)
    dims = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check(dims[1] > dims[0], "the rotated CR2 is upright in the preview: %s" % dims)
    # develop everything (the best quality): == the command line version; then with the best quality off; then from the JPEGs
    film = page.evaluate("FD.S.films[FD.S.film].full")
    flim = re.sub(r"[^A-Za-z0-9]+", "_", film).strip("_")
    film_no = page.evaluate("FD.S.film") + 1

    def compare_all(z, flags, label):
        work = tempfile.mkdtemp()
        for name in z.namelist():
            base = name.rsplit("_" + flim, 1)[0]
            src = [f for f in os.listdir(tmp) if f.rsplit(".", 1)[0] == base][0]
            cli_make(work, film_no, "L", flags, os.path.join(tmp, src))
            check(z.read(name) == open(os.path.join(work, name), "rb").read(), "%s: %s == command line" % (name, label))
        shutil.rmtree(work, ignore_errors=True)

    def develop_all(label, flags):
        page.click("#selAll")
        run_and_wait(page)
        metas = page.eval_on_selector_all("#list li .meta", "els => els.map(e => e.textContent)")
        n_raw = sum(1 for m in metas if "z RAW" in m)
        check(all("błąd" not in m for m in metas), "%s: no errors: %s" % (label, metas))
        return zip_of(page), n_raw

    z, n_raw = develop_all("best quality", [])
    check(n_raw == 7, "seven photos are developed from the sensor data (CR2, DNG, CR3), the fake CR3 from its JPEG (z RAW: %d)" % n_raw)
    compare_all(z, [], "RAW")
    shot(page, "sensor_3_done")
    page.uncheck("#chkHq")
    z, _ = develop_all("best quality off", ["--hq", "0"])
    compare_all(z, ["--hq", "0"], "RAW, hq off")
    page.check("#chkHq")
    page.click('#srcSeg button[data-v="1"]')
    z, n_raw = develop_all("JPG z pliku", ["--usejpg", "1"])
    check(n_raw == 0, "JPG z pliku: nothing is developed from the sensor data")
    compare_all(z, ["--usejpg", "1"], "JPG z pliku")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_crop(browser):
    """the crop tool and the free size of the result: a frame with handles, the lock of the ratio and the list of ratios, the same
    ratio for all photos, any size (also bigger than the photo); the developed files == the command line version, byte for byte"""
    from PIL import Image, ImageOps
    import io
    tmp = tempfile.mkdtemp()
    for n in ("synth_small.jpg", "synth_portrait.cr2", "synth_landscape.cr2"):
        shutil.copy(os.path.join(photos, n), tmp)
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_selector("#list li")
    page.click("#list li:has-text('synth_small.jpg')")
    wait_idle(page)
    page.wait_for_function("FD.cropFrame() !== null", timeout=60000)
    fr = page.evaluate("FD.cropFrame()")
    check(fr == {"W": 1200, "H": 800}, "the frame of the photo is known: %s" % fr)
    check(page.is_enabled("#btnCrop"), "the crop button is on")
    check("Pełny" in page.inner_text("#outMode") and page.input_value("#outMode") == "full", "the size of the result starts as the full size")
    check(page.locator("#sizes").count() == 0, "the fixed sizes L / M / S are gone")
    check("1200 × 800" in page.inner_text("#outInfo"), "the size of this photo is told: " + page.inner_text("#outInfo"))

    def rect():
        return page.evaluate("(() => { const r = FD.CROP.rect; return [r.x0, r.y0, r.x1, r.y1]; })()")

    def dims(r):
        return r[2] - r[0], r[3] - r[1]

    def drag(sel, dx, dy):
        bb = page.locator(sel).bounding_box()
        x, y = bb["x"] + bb["width"] / 2, bb["y"] + bb["height"] / 2
        page.mouse.move(x, y)
        page.mouse.down()
        page.mouse.move(x + dx / 2, y + dy / 2, steps=4)
        page.mouse.move(x + dx, y + dy, steps=6)
        page.mouse.up()

    page.click("#btnCrop")
    page.wait_for_function("!document.querySelector('#cropUi').classList.contains('hidden')", timeout=30000)
    check(page.is_visible("#cropBar") and page.is_visible("#cropRect"), "the crop tool opens: the bar and the frame")
    opts = page.eval_on_selector_all("#cropAr option", "els => els.map(e => e.textContent)")
    check(any("3 : 2" in o for o in opts) and any("1 : 1" in o for o in opts) and any("16 : 9" in o for o in opts) and any("Oryginał" in o for o in opts)
          and any("Dowolne" in o for o in opts) and any("Własne" in o for o in opts), "the list of ratios: %s" % opts)
    check(rect() == [0, 0, 1200, 800], "the frame starts as the whole picture: %s" % rect())
    shot(page, "crop_1_open")

    page.select_option("#cropAr", "1:1")
    r = rect()
    check(dims(r) == (800, 800) and r[0] == 200, "ratio 1 : 1 gives the biggest square in the middle: %s" % r)
    drag("#cropRect .ch[data-h='se']", -90, -15)
    r2 = rect()
    check(dims(r2)[0] == dims(r2)[1] and dims(r2)[0] < 800 and r2[0] == r[0] and r2[1] == r[1], "dragging a corner keeps the square and the opposite corner: %s" % r2)
    drag("#cropRect", 40, 20)
    r3 = rect()
    check(dims(r3) == dims(r2) and r3[0] > r2[0] and r3[1] >= r2[1], "dragging inside the frame moves it, the size stays: %s" % r3)
    page.select_option("#cropAr", "free")
    drag("#cropRect .ch[data-h='e']", -50, 30)
    r4 = rect()
    check(dims(r4)[0] < dims(r3)[0] and dims(r4)[1] == dims(r3)[1] and r4[:2] == r3[:2], "without the lock a side moves alone: %s" % r4)
    drag("#cropRect .ch[data-h='nw']", 40, 40)
    r5 = rect()
    check(r5[0] > r4[0] and r5[1] > r4[1] and r5[2:] == r4[2:], "and so does a corner: %s" % r5)
    page.select_option("#cropAr", "16:9")
    r6 = rect()
    check(dims(r6)[0] * 9 == dims(r6)[1] * 16, "ratio 16 : 9 is exact: %s" % r6)
    drag("#cropRect .ch[data-h='s']", 0, 25)
    r7 = rect()
    check(dims(r7)[0] * 9 == dims(r7)[1] * 16, "dragging an edge with the lock keeps 16 : 9: %s" % r7)
    page.click("#cropFlip")
    r8 = rect()
    check(dims(r8)[0] * 16 == dims(r8)[1] * 9 and dims(r8)[0] < dims(r8)[1], "the button turns the ratio round (9 : 16): %s" % r8)
    page.select_option("#cropAr", "custom")
    page.fill("#cropCa", "7"); page.dispatch_event("#cropCa", "change")
    page.fill("#cropCb", "3"); page.dispatch_event("#cropCb", "change")
    r9 = rect()
    check(page.is_visible("#cropCa") and (dims(r9)[0] * 3 == dims(r9)[1] * 7 or dims(r9)[0] * 7 == dims(r9)[1] * 3), "an own ratio 7 : 3: %s" % r9)
    page.select_option("#cropAr", "orig")
    r10 = rect()
    check(dims(r10)[0] * 2 == dims(r10)[1] * 3, "\"Oryginał\" is the ratio of the photo (3 : 2): %s" % r10)
    page.click("#cropReset")
    check(rect() == [0, 0, 1200, 800], "\"Wyzeruj\" brings back the whole frame: %s" % rect())
    page.select_option("#cropAr", "free")
    page.click("#cropDone")
    check(page.evaluate("FD.S.sel.crop") is None and not page.evaluate("FD.CROP.on"), "the whole frame is no crop at all")

    # a crop that is kept: 3 : 2, smaller than the photo
    page.keyboard.press("c")
    page.wait_for_function("FD.CROP.on === true")
    page.select_option("#cropAr", "3:2")
    drag("#cropRect .ch[data-h='se']", -300, -200)
    r = rect()
    check(dims(r)[0] * 2 == dims(r)[1] * 3 and dims(r)[0] < 1000, "a 3 : 2 crop: %s" % r)
    shot(page, "crop_2_drawn")
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Enter")                                     # Enter = "Gotowe"
    page.wait_for_function("FD.CROP.on === false")
    crop = page.evaluate("FD.S.sel.crop")
    check(crop is not None and abs((crop["x1"] - crop["x0"]) / 1e6 * 1200 - dims(r)[0]) < 0.01, "the crop is kept for the photo: %s" % crop)
    check("z kadrem" in page.inner_text("#list li.sel .meta"), "the list says so: " + page.inner_text("#list li.sel .meta"))
    page.wait_for_function("Math.abs(FD.V.fw - %d) <= 2 && Math.abs(FD.V.fh - %d) <= 2" % dims(r), timeout=60000)
    check(page.evaluate("FD.V.key") != "" and page.is_hidden("#cropUi"), "the picture on the screen is the cropped one, the frame is gone")
    wait_idle(page)
    shot(page, "crop_3_done")

    # Esc leaves the crop as it was
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("c")
    page.wait_for_function("FD.CROP.on === true && !document.querySelector('#cropUi').classList.contains('hidden')", timeout=60000)   # (the whole frame is made first)
    check(rect() != [0, 0, 1200, 800] and dims(rect())[0] * 2 == dims(rect())[1] * 3, "opened on a cropped photo: the frame is the crop, the list shows 3 : 2 (%s, %s)" % (rect(), page.input_value("#cropAr")))
    page.select_option("#cropAr", "free")
    drag("#cropRect .ch[data-h='nw']", 30, 30)
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Escape")
    page.wait_for_function("FD.CROP.on === false")
    check(page.evaluate("FD.S.sel.crop") == crop, "Esc cancels: the crop is as it was")

    # the size of the result: any, also bigger than the photo
    page.select_option("#outMode", "long")
    page.fill("#outPx", "900"); page.dispatch_event("#outPx", "change")
    ow, oh = page.evaluate("[FD.outDims(%d, %d).w, FD.outDims(%d, %d).h]" % (dims(r)[0], dims(r)[1], dims(r)[0], dims(r)[1]))
    check((ow, oh) == (900, 600) or (ow == 900 and abs(oh - 600) <= 1), "the long side 900 px: %d x %d" % (ow, oh))
    check("900 × 600" in page.inner_text("#outInfo") and "powiększone" in page.inner_text("#outInfo"), "the line tells the size and that it is enlarged: " + page.inner_text("#outInfo"))
    page.select_option("#outMode", "w")
    page.fill("#outPx", "20000"); page.dispatch_event("#outPx", "change")
    check(page.input_value("#outPx") == "16384" and "bad" in (page.get_attribute("#outInfo", "class") or ""), "too big: the number stops at 16384 and the line warns: " + page.inner_text("#outInfo"))
    page.select_option("#outMode", "long")
    page.fill("#outPx", "900"); page.dispatch_event("#outPx", "change")

    # develop this photo and compare with the command line
    page.click("#selNone")
    page.locator("#list li", has_text="synth_small.jpg").locator(".ck").check()
    run_and_wait(page)
    z = zip_of(page)
    names = z.namelist()
    check(len(names) == 1 and names[0].endswith("_900px.jpg"), "the file name says the size: %s" % names)
    im = Image.open(io.BytesIO(z.read(names[0])))
    check(im.size == (900, 600), "the file is 900 x 600: %s" % (im.size,))
    work = tempfile.mkdtemp()
    box = ",".join("%.6f" % (crop[k] / 1e6) for k in ("x0", "y0", "x1", "y1"))
    subprocess.run([cli, "--quiet", "--film", "1", "--long", "900", "--crop", box, "--quality", PAGE_QUALITY, "--out", work, os.path.join(tmp, "synth_small.jpg")], check=True, capture_output=True)
    check(z.read(names[0]) == open(os.path.join(work, names[0]), "rb").read(), "%s: page == command line (crop + enlarged)" % names[0])
    shutil.rmtree(work, ignore_errors=True)

    # the same ratio for all photos
    page.select_option("#outMode", "full")
    page.click("#list li:has-text('synth_portrait.cr2')")
    page.wait_for_function("FD.S.pv && FD.S.pv.file.name === 'synth_portrait.cr2' && FD.cropFrame() !== null", timeout=60000)
    wait_idle(page)
    fr2 = page.evaluate("FD.cropFrame()")
    check(fr2["H"] > fr2["W"], "a portrait photo has an upright frame: %s" % fr2)
    page.click("#btnCrop")
    page.wait_for_function("FD.CROP.on === true")
    page.select_option("#cropAr", "16:9")
    r = rect()
    check(dims(r)[1] * 9 == dims(r)[0] * 16, "on a portrait photo 16 : 9 stands upright (9 : 16): %s" % r)
    page.click("#cropAll")
    ar = page.evaluate("FD.S.files.map(f => [f.name, f.crop ? 'own' : null, f.cropAr ? f.cropAr.w + ':' + f.cropAr.h : null])")
    check(any(a[0] == "synth_landscape.cr2" and a[2] == "16:9" for a in ar) and any(a[0] == "synth_small.jpg" and a[1] == "own" for a in ar),
          "\"Dla wszystkich\": the others get 16 : 9, the one with an own crop keeps it: %s" % ar)
    page.click("#cropDone")
    page.wait_for_function("FD.CROP.on === false")
    check(page.evaluate("!!FD.S.sel.crop"), "and this one has the crop that was drawn")
    # drawn like this one the portrait photo has the biggest 2 : 3 in the middle, which is what the others get
    page.click("#selNone")
    for n in ("synth_portrait.cr2", "synth_landscape.cr2"):
        page.locator("#list li", has_text=n).locator(".ck").check()
    run_and_wait(page)
    z = zip_of(page)
    names = [n for n in z.namelist() if "synth_small" not in n]
    check(len(names) == 2, "two files are developed: %s" % names)
    for n in names:
        im = ImageOps.exif_transpose(Image.open(io.BytesIO(z.read(n))))
        w, h = im.size
        want = (w * 16 == h * 9) if "portrait" in n else (w * 9 == h * 16)
        check(want, "%s: the picture has the ratio (%d x %d)" % (n, w, h))
    work = tempfile.mkdtemp()                 # the landscape photo got the ratio from "Dla wszystkich" (the portrait one has the rectangle drawn by hand)
    subprocess.run([cli, "--quiet", "--film", "1", "--ratio", "16:9", "--quality", PAGE_QUALITY, "--out", work, os.path.join(tmp, "synth_landscape.cr2")], check=True, capture_output=True)
    for n in names:
        if "landscape" in n:
            check(z.read(n) == open(os.path.join(work, n), "rb").read(), "%s: page == command line (--ratio 16:9)" % n)
    shutil.rmtree(work, ignore_errors=True)

    # switching to another photo while the tool is open keeps the crop of the first
    page.click("#list li:has-text('synth_landscape.cr2')")
    page.wait_for_function("FD.S.pv && FD.S.pv.file.name === 'synth_landscape.cr2' && FD.cropFrame() !== null", timeout=60000)
    wait_idle(page)
    page.click("#btnCrop")
    page.wait_for_function("FD.CROP.on === true && !document.querySelector('#cropUi').classList.contains('hidden')", timeout=60000)
    page.select_option("#cropAr", "1:1")
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("ArrowRight")
    page.wait_for_function("FD.CROP.on === false")
    got = page.evaluate("FD.S.files.filter(f => f.name === 'synth_landscape.cr2')[0].crop")
    check(got is not None and abs((got["x1"] - got["x0"]) * 1200 - (got["y1"] - got["y0"]) * 800) < 2e3 * 3, "another photo: the crop (1 : 1) stays with the first one: %s" % got)
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_rotate(browser):
    """straightening in the crop tool (like Lightroom): the angle (number, slider, a drag outside the frame, the line along the horizon),
    quarter turns, the frame always inside the turned picture, Esc, the removal; the developed file == the command line version"""
    from PIL import Image, ImageOps
    import io
    import math
    tmp = tempfile.mkdtemp()
    for n in ("synth_small.jpg", "synth_portrait.cr2"):
        shutil.copy(os.path.join(photos, n), tmp)
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_selector("#list li")
    page.click("#list li:has-text('synth_small.jpg')")
    wait_idle(page)
    page.wait_for_function("FD.cropFrame() !== null", timeout=60000)

    def rect():
        return page.evaluate("(() => { const r = FD.CROP.rect; return [r.x0, r.y0, r.x1, r.y1]; })()")

    def inside(tol=1):
        return page.evaluate("FD.rectInQuad(FD.CROP.rect, FD.cropFrame(), FD.CROP.rot, %s)" % tol)

    def shown():
        page.wait_for_function("FD.CROP.on === true && !document.querySelector('#cropUi').classList.contains('hidden')", timeout=60000)

    page.click("#btnCrop")
    shown()
    for sel in ("#cropAng", "#cropAngN", "#cropRuler", "#cropTurnL", "#cropTurnR", "#cropRing"):
        check(page.locator(sel).count() == 1, "the crop tool has " + sel)
    check(page.evaluate("FD.CROP.rot") == 0 and page.input_value("#cropAngN") in ("0", "0.0"), "the angle starts at 0")
    base = rect()

    # the number box
    page.fill("#cropAngN", "10"); page.dispatch_event("#cropAngN", "change")
    check(page.evaluate("FD.CROP.rot") == 10000, "10 degrees typed: %s" % page.evaluate("FD.CROP.rot"))
    check(page.evaluate("document.querySelector('#pvImg').style.transform") == "rotate(10deg)", "the picture on the screen is turned (clockwise)")
    r10 = rect()
    check(inside() and (r10[2] - r10[0]) < (base[2] - base[0]) and (r10[3] - r10[1]) < (base[3] - base[1]), "the frame shrank to lie inside the turned picture: %s -> %s" % (base, r10))
    check(abs((r10[2] - r10[0]) * 800 - (r10[3] - r10[1]) * 1200) <= 1200 and abs((r10[0] + r10[2]) / 2 - 600) <= 1 and abs((r10[1] + r10[3]) / 2 - 400) <= 1, "(it keeps the ratio and its middle: %s)" % r10)
    shot(page, "rot_1_ten")
    page.fill("#cropAngN", "99"); page.dispatch_event("#cropAngN", "change")
    check(page.evaluate("FD.CROP.rot") == 45000, "the angle stops at 45 degrees")
    page.fill("#cropAngN", "0"); page.dispatch_event("#cropAngN", "change")
    check(rect() == base and page.evaluate("document.querySelector('#pvImg').style.transform") in ("", "rotate(0deg)"), "back to 0: the frame is as big as before and the picture is straight: %s %s" % (rect(), page.evaluate("document.querySelector('#pvImg').style.transform")))

    # the slider: the frame grows back when the angle goes back (while the slider is being moved)
    page.evaluate("(() => { const a = document.querySelector('#cropAng'); a.value = 6; a.dispatchEvent(new Event('input', {bubbles: true})); })()")
    r6 = rect()
    page.evaluate("(() => { const a = document.querySelector('#cropAng'); a.value = 3; a.dispatchEvent(new Event('input', {bubbles: true})); })()")
    r3 = rect()
    check(page.evaluate("FD.CROP.rot") == 3000 and inside() and (r3[2] - r3[0]) > (r6[2] - r6[0]), "slider 6 then 3 degrees: the frame grows back: %s %s" % (r6, r3))
    page.evaluate("(() => { const a = document.querySelector('#cropAng'); a.value = 0; a.dispatchEvent(new Event('input', {bubbles: true})); a.dispatchEvent(new Event('change', {bubbles: true})); })()")
    check(rect() == base, "slider back to 0: the first frame again")

    # a drag in the zone outside the frame turns the picture
    bb = page.locator("#cropRect").bounding_box()
    x, y = bb["x"] + bb["width"] + 32, bb["y"] + bb["height"] / 2
    page.mouse.move(x, y); page.mouse.down(); page.mouse.move(x, y - 40, steps=5); page.mouse.move(x, y - 80, steps=5)
    check(page.evaluate("FD.CROP.rot") < -1000 and "fine" in (page.get_attribute("#cropRect", "class") or ""), "dragging outside the frame turns it (up on the right = counter-clockwise), the fine grid shows: %s" % page.evaluate("FD.CROP.rot"))
    page.mouse.up()
    check(inside() and "fine" not in (page.get_attribute("#cropRect", "class") or ""), "and the frame is inside the picture; the fine grid is gone")
    page.click("#cropReset")
    check(page.evaluate("FD.CROP.rot") == 0 and rect() == base, "\"Wyzeruj\" brings back the angle")

    # the line along the horizon
    page.click("#cropRuler")
    check(page.is_visible("#cropRulerPad") and page.get_attribute("#cropRuler", "aria-pressed") == "true", "\"Wyprostuj\" shows the pad for the line")
    bb = page.locator("#cropRect").bounding_box()
    x, y = bb["x"] + 30, bb["y"] + 60
    dx = 300.0
    page.mouse.move(x, y); page.mouse.down(); page.mouse.move(x + dx / 2, y + dx / 2 * math.tan(math.radians(5)), steps=4); page.mouse.move(x + dx, y + dx * math.tan(math.radians(5)), steps=4); page.mouse.up()
    check(abs(page.evaluate("FD.CROP.rot") + 5000) <= 150 and inside() and page.is_hidden("#cropRulerPad"), "a line that falls 5 degrees to the right: the picture is turned 5 degrees back: %s" % page.evaluate("FD.CROP.rot"))
    page.click("#cropReset")

    # quarter turns
    fr0 = page.evaluate("FD.cropFrame()")
    page.select_option("#cropAr", "free")
    page.evaluate("document.querySelector('#cropTurnR').click()")
    shown()
    fr1 = page.evaluate("FD.cropFrame()")
    check(page.evaluate("FD.CROP.turn") == 1 and fr1 == {"W": fr0["H"], "H": fr0["W"]}, "a quarter turn to the right: the frame is upright now: %s" % fr1)
    check(rect() == [0, 0, fr1["W"], fr1["H"]], "(the frame follows)")
    page.keyboard.press("Control+[")
    shown()
    check(page.evaluate("FD.CROP.turn") == 0 and page.evaluate("FD.cropFrame()") == fr0, "Ctrl + [ turns it back")
    page.keyboard.press("Control+]")
    shown()
    check(page.evaluate("FD.CROP.turn") == 1, "Ctrl + ] turns to the right")
    shot(page, "rot_2_turned")

    # a kept crop: turned 90 degrees to the right and straightened by 4.5 degrees
    page.select_option("#cropAr", "3:2")
    page.fill("#cropAngN", "4.5"); page.dispatch_event("#cropAngN", "change")
    check(inside(), "3 : 2 with 4.5 degrees on a turned picture: the frame is inside: %s" % rect())
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Enter")
    page.wait_for_function("FD.CROP.on === false")
    f = page.evaluate("({rot: FD.S.sel.rot, turn: FD.S.sel.turn, crop: FD.S.sel.crop})")
    check(f["rot"] == 4500 and f["turn"] == 1 and f["crop"] is not None, "kept for the photo: %s" % f)
    check("obrócone" in page.inner_text("#list li.sel .meta"), "the list says so: " + page.inner_text("#list li.sel .meta"))
    check("obrócone o 4,5°" in page.inner_text("#cropSum") and "w prawo" in page.inner_text("#cropSum"), "the group \"Kadr\" tells it: " + page.inner_text("#cropSum"))
    check("mod" in (page.get_attribute('#rail .rb[data-g="crop"]', "class") or ""), "the icon of the group shows a dot")
    page.wait_for_function("FD.V.key !== '' && document.querySelector('#cropUi').classList.contains('hidden')", timeout=60000)
    wait_idle(page)
    pw, ph = page.evaluate("[FD.V.fw, FD.V.fh]")
    crop = f["crop"]
    cw, ch = (crop["x1"] - crop["x0"]) / 1e6 * fr1["W"], (crop["y1"] - crop["y0"]) / 1e6 * fr1["H"]
    check(abs(pw - cw) <= 2 and abs(ph - ch) <= 2, "the picture on the screen has the size of the crop (%dx%d, crop %.0fx%.0f)" % (pw, ph, cw, ch))
    shot(page, "rot_3_done")

    # Esc leaves the angle as it was
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("c")
    shown()
    check(page.evaluate("FD.CROP.rot") == 4500 and page.evaluate("FD.CROP.turn") == 1 and inside(), "opened again: the angle and the turn are those of the photo")
    page.fill("#cropAngN", "-12"); page.dispatch_event("#cropAngN", "change")
    page.evaluate("document.querySelector('#cropTurnR').click()")
    shown()
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Escape")
    page.wait_for_function("FD.CROP.on === false")
    check(page.evaluate("[FD.S.sel.rot, FD.S.sel.turn]") == [4500, 1], "Esc: angle and turn are as they were: %s" % page.evaluate("[FD.S.sel.rot, FD.S.sel.turn]"))
    wait_idle(page)

    # developed == the command line version
    page.click("#selNone")
    page.locator("#list li", has_text="synth_small.jpg").locator(".ck").check()
    run_and_wait(page)
    z = zip_of(page)
    names = z.namelist()
    check(len(names) == 1, "one file: %s" % names)
    work = tempfile.mkdtemp()
    box = ",".join("%.6f" % (crop[k] / 1e6) for k in ("x0", "y0", "x1", "y1"))
    subprocess.run([cli, "--quiet", "--film", "1", "--crop", box, "--angle", "4.5", "--turn", "1", "--quality", PAGE_QUALITY, "--out", work, os.path.join(tmp, "synth_small.jpg")], check=True, capture_output=True)
    check(z.read(names[0]) == open(os.path.join(work, names[0]), "rb").read(), "%s: page == command line (--crop --angle 4.5 --turn 1)" % names[0])
    im = ImageOps.exif_transpose(Image.open(io.BytesIO(z.read(names[0]))))             # the file keeps the sensor's order of pixels and tells the turn in its EXIF
    check(abs(im.size[0] - cw) <= 2 and abs(im.size[1] - ch) <= 2, "the file has the size of the crop: %s" % (im.size,))
    shutil.rmtree(work, ignore_errors=True)

    # removal
    page.click("#btnCropClear")
    check(page.evaluate("[FD.S.sel.rot, FD.S.sel.turn, FD.S.sel.crop]") == [None, None, None], "\"Usuń kadr i obrót\" removes all three")
    check("mod" not in (page.get_attribute('#rail .rb[data-g="crop"]', "class") or ""), "the dot on the icon is gone")
    # a portrait photo (the camera turns it): a quarter turn keeps it upright for the engine
    page.click("#list li:has-text('synth_portrait.cr2')")
    page.wait_for_function("FD.S.pv && FD.S.pv.file.name === 'synth_portrait.cr2' && FD.cropFrame() !== null", timeout=60000)
    wait_idle(page)
    frp = page.evaluate("FD.cropFrame()")
    page.click("#btnCrop")
    shown()
    page.evaluate("document.querySelector('#cropTurnL').click()")
    shown()
    check(page.evaluate("FD.cropFrame()") == {"W": frp["H"], "H": frp["W"]}, "portrait photo, a quarter turn to the left: now the frame is wide: %s" % page.evaluate("FD.cropFrame()"))
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Enter")
    page.wait_for_function("FD.CROP.on === false")
    check(page.evaluate("FD.S.sel.turn") == 3 and page.evaluate("FD.S.sel.crop") is None, "turn 3 (= 90 degrees to the left) without a crop")
    wait_idle(page)
    check(page.evaluate("FD.V.fw > FD.V.fh"), "the picture on the screen is wide now: %s" % page.evaluate("[FD.V.fw, FD.V.fh]"))
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


def scenario_groups(browser):
    """the right panel: one icon for every group of settings (RapidRAW style), the group "Zapis" (opened by the button at the top), nothing about
    the engine in the top bar, Tab hides the panels"""
    page = open_page(browser)
    page.set_input_files("#inDir", photos)
    page.wait_for_selector("#list li")
    wait_idle(page)
    rb = page.eval_on_selector_all("#rail .rb", "els => els.map(e => e.dataset.g)")
    check(rb == ["film", "light", "color", "crop", "source", "export"], "the strip of icons: %s" % rb)
    check(page.evaluate("document.querySelectorAll('#adj .grp:not(.hidden)').length") == 1, "one group is shown at a time")
    check(page.is_hidden("#exp") and page.is_hidden("#btnRun"), "the saving part is not shown all the time")
    check(not page.is_visible("#chipEngine"), "nothing about the engine in the top bar")
    for g, title, sec in (("film", "Film i ziarno", "#sl-grain"), ("light", "Światło", "#sl-expo"), ("color", "Kolor", "#wbChips"), ("crop", "Kadr i obrót", "#btnCropOpen"), ("source", "Źródło zdjęcia", "#srcSeg")):
        page.click('#rail .rb[data-g="%s"]' % g)
        check(page.inner_text("#rtitle") == title and page.is_visible(sec) and page.get_attribute('#rail .rb[data-g="%s"]' % g, "aria-selected") == "true", "icon %s: the group %s is shown" % (g, title))
    check(page.is_hidden("#sl-grain") and page.is_hidden("#sl-expo"), "(and the others are hidden)")
    page.click('#rail .rb[data-g="film"]')
    check(page.is_visible("#rsFilm") and page.is_hidden("#rsScene"), "the film group has \"Resetuj film\"")
    page.click('#rail .rb[data-g="light"]')
    check(page.is_hidden("#rsFilm") and page.is_visible("#rsScene"), "the light group has \"Resetuj korekty\"")
    set_number(page, "expo", 0.5)
    check("mod" in page.get_attribute('#rail .rb[data-g="light"]', "class"), "a changed slider puts a dot on the icon of its group")
    check("mod" not in page.get_attribute('#rail .rb[data-g="color"]', "class"), "(and not on the others)")
    page.click('#rail .rb[data-g="film"]')
    check(page.locator('.grp[data-g="film"].single').count() == 1, "one section in the group: no second title")
    # the button at the top
    page.click('#rail .rb[data-g="crop"]')
    page.click("#btnSave")
    check(page.is_visible("#btnRun") and page.get_attribute('#rail .rb[data-g="export"]', "aria-selected") == "true" and page.is_visible("#outMode"), "\"Zapisz\" at the top opens the group \"Zapis\" (size, quality, develop)")
    check("Do wywołania:" in page.inner_text("#expSel"), "it tells what will be developed: " + page.inner_text("#expSel"))
    page.click('#rail .rb[data-g="film"]')
    page.keyboard.press("Control+s")
    check(page.is_visible("#btnRun"), "Ctrl + S opens it too")
    # Tab hides both panels
    page.evaluate("document.activeElement && document.activeElement.blur()")
    page.keyboard.press("Tab")
    check(page.evaluate("document.querySelector('#app').classList.contains('lc') && document.querySelector('#app').classList.contains('rc')"), "Tab hides both panels")
    page.keyboard.press("Tab")
    check(not page.evaluate("document.querySelector('#app').classList.contains('lc') || document.querySelector('#app').classList.contains('rc')"), "and brings them back")
    # the choice of the group is remembered
    page.click('#rail .rb[data-g="color"]')
    page.reload()
    page.wait_for_function("document.querySelector('#chipEngine').className.indexOf('ok') >= 0", timeout=30000)
    wrap_reveal(page)
    page.wait_for_function("document.querySelector('#rail .rb[data-g=\"color\"]').getAttribute('aria-selected') === 'true'", timeout=10000)
    check(True, "the group that was open stays open after a reload")
    # the button at the top shows the progress while developing
    page.set_input_files("#inDir", photos)
    page.wait_for_selector("#list li")
    page.click("#selNone")
    page.locator("#list li").first.locator(".ck").check()
    page.click("#btnSave")
    page.click("#btnRun")
    page.wait_for_function("document.querySelector('#saveTxt').textContent.indexOf('Wywołuję') === 0", timeout=30000)
    check(True, "the button at the top shows \"Wywołuję\" while photos are developed")
    page.wait_for_function("document.querySelector('#runInfo').textContent.indexOf('Gotowe') === 0", timeout=300000)
    check(page.inner_text("#saveTxt") == "Zapisz", "and \"Zapisz\" again when it is over")
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    page.context.close()


def scenario_grain(browser):
    """the preview is counted like the file (4:4:4, every pixel exact), the full size picture is the finished one (size of the result and
    quality of the file): the grain looks as it will in the file; 100 % = one pixel of the finished picture"""
    from PIL import Image
    import io
    tmp = tempfile.mkdtemp()
    shutil.copy(os.path.join(photos, "synth_small.jpg"), tmp)
    page = open_page(browser)
    page.set_input_files("#inDir", tmp)
    page.wait_for_selector("#list li")
    wait_idle(page)
    page.wait_for_function("FD.cropFrame() !== null", timeout=60000)
    check(page.evaluate("FD.pvParams({size: 102, quality: 92}).x.hq") == 1, "the preview is counted like the file (hq)")
    sampling = page.evaluate("""async () => {
      const b = new Uint8Array(await FD.S.pv.afterBlob.arrayBuffer());
      for (let i = 2; i + 9 < b.length; ) {
        if (b[i] !== 0xFF) { i++; continue; }
        const m = b[i + 1], len = (b[i + 2] << 8) | b[i + 3];
        if (m === 0xC0 || m === 0xC1 || m === 0xC2) { const n = b[i + 9]; const r = []; for (let c = 0; c < n; c++) r.push(b[i + 10 + 3 * c + 1]); return r; }
        i += 2 + len;
      }
      return null;
    }""")
    check(sampling == [0x11, 0x11, 0x11], "the colours of the preview are in full resolution (4:4:4): %s" % sampling)
    page.click('#rail .rb[data-g="export"]')
    page.uncheck("#chkHq")
    wait_idle(page)
    check(page.evaluate("FD.pvParams({size: 102, quality: 92}).x.hq") == 0, "\"Najwyższa jakość\" off: the preview is counted the camera way too")
    page.check("#chkHq")
    wait_idle(page)

    # 100 % = a pixel of the finished picture
    check(page.evaluate("FD.outK()") == 1, "full size: one pixel of the file is one pixel of the photo")
    page.select_option("#outMode", "long")
    page.fill("#outPx", "600"); page.dispatch_event("#outPx", "change")
    check(abs(page.evaluate("FD.outK()") - 2) < 0.01, "a file of 600 px from a photo of 1200 px: one pixel of the file = 2 pixels of the photo")
    page.click("#z100")
    check(abs(page.evaluate("FD.V.s") - 0.5) < 0.01 and page.inner_text("#zVal") == "100 %", "\"100 %%\" shows the finished picture 1:1 (scale %.2f of the photo, %s)" % (page.evaluate("FD.V.s"), page.inner_text("#zVal")))
    page.wait_for_function("FD.S.pv.afterFull !== null", timeout=120000)
    wait_idle(page)
    nat = page.evaluate("[document.querySelector('#pvImg').naturalWidth, document.querySelector('#pvImg').naturalHeight]")
    check(nat == [600, 400], "the picture shown is the file's size (600 x 400): %s" % nat)
    check("jak w pliku" in page.inner_text("#pvCap"), "the line under the picture says it is the finished one: " + page.inner_text("#pvCap"))
    # ... and it IS the finished picture: the same pixels as the developed file
    detail = page.evaluate("""async () => { const r = await fetch(FD.S.pv.afterFull); return Array.from(new Uint8Array(await r.arrayBuffer())); }""")
    page.click('#rail .rb[data-g="export"]')
    page.click("#selNone")
    page.locator("#list li").first.locator(".ck").check()
    run_and_wait(page)
    z = zip_of(page)
    final = Image.open(io.BytesIO(z.read(z.namelist()[0]))).convert("RGB")
    prev = Image.open(io.BytesIO(bytes(detail))).convert("RGB")
    same = final.size == prev.size and list(final.getdata()) == list(prev.getdata())
    check(same, "the full size picture on the screen has exactly the pixels of the developed file (%s, %s)" % (final.size, prev.size))
    # the button in the film group
    page.select_option("#outMode", "full")
    page.click('#rail .rb[data-g="film"]')
    page.click("#zFit")
    page.click("#btnGrain")
    check(abs(page.evaluate("FD.V.s") - 1) < 0.01, "\"Pokaż ziarno jak w pliku\" zooms to 100 %%: %s" % page.evaluate("FD.V.s"))
    check(not page.errors, "no errors in the browser console: %s" % page.errors[:3])
    shutil.rmtree(tmp, ignore_errors=True)
    page.context.close()


SCENARIOS = [("main", scenario_main), ("zoom", scenario_zoom), ("wb", scenario_wb), ("bw", scenario_bw), ("parallel", scenario_parallel),
             ("inline", scenario_inline), ("portrait", scenario_portrait), ("folder", scenario_folder), ("layout", scenario_layout), ("curves", scenario_curves), ("raw", scenario_raw), ("sensor", scenario_sensor), ("crop", scenario_crop), ("rotate", scenario_rotate), ("groups", scenario_groups), ("grain", scenario_grain)]
with sync_playwright() as p:
    browser = p.chromium.launch()
    for name, fn in SCENARIOS:
        if only and only != name:
            continue
        print("\n== " + name, flush=True)
        if name == "sensor" and not os.path.exists(os.path.join(all_photos, "synth_raw_strips.dng")):
            print("(skipped: no synth_raw_*.cr2 / dng in the folder: use tests/make_samples.py)")
            continue
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
