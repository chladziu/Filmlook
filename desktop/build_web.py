#!/usr/bin/env python3
"""Puts the browser version together: web/page.html + web/engine_core.js + the .wasm engine + the licences -> ONE html file.

usage: build_web.py <engine.wasm> <output.html>

The result needs nothing else: no internet, no installation. The engine is inside the page as base64."""
import base64
import datetime
import html
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def git(*args):
    try:
        return subprocess.check_output(("git",) + args, cwd=ROOT, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return ""


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    wasm_path, out_path = sys.argv[1], sys.argv[2]
    page = read(os.path.join(HERE, "web", "page.html"))
    core = read(os.path.join(HERE, "web", "engine_core.js"))
    with open(wasm_path, "rb") as f:
        wasm = f.read()

    lic = read(os.path.join(ROOT, "modules", "filmlook", "engine", "LICENSE"))
    third = read(os.path.join(ROOT, "THIRD_PARTY_LICENSES.txt"))
    licence = "SILNIK (Soup Engine, GPL v2)\n\n" + lic.strip() + "\n\n\n" + third.strip() + "\n"

    prog = git("log", "-1", "--format=%h", "--", "desktop") or "?"
    eng = git("log", "-1", "--format=%h", "--", "modules/filmlook") or "?"
    when = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    build = "program %s · silnik %s · zbudowano %s" % (prog, eng, when)

    if "</script" in core.lower():
        sys.exit("engine_core.js must not contain </script")
    for marker in ("__LICENSE__", "__BUILD__", "__WASM_B64__", "__ENGINE_CORE__"):
        if page.count(marker) != 1:
            sys.exit("page.html: the marker %s must occur exactly once" % marker)
    page = page.replace("__LICENSE__", html.escape(licence, quote=False))
    page = page.replace("__BUILD__", html.escape(build, quote=False))
    page = page.replace("__WASM_B64__", base64.b64encode(wasm).decode("ascii"))
    page = page.replace("__ENGINE_CORE__", core)    # last: the text of the script may contain anything
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print("wrote %s (%d KB; engine %d KB; %s)" % (out_path, os.path.getsize(out_path) // 1024, len(wasm) // 1024, build))


if __name__ == "__main__":
    main()
