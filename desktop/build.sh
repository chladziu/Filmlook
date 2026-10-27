#!/usr/bin/env bash
# FILM LOOK DESKTOP - builds what the user gets:
#   desktop/build.sh web   ->  desktop/out/FILMLOOK_DESKTOP.html   (one page, works offline in any browser; needs clang with the
#                                                                    wasm32 target + lld, and python3)
#   desktop/build.sh cli   ->  desktop/out/filmlook_cli[.exe]      (command line version; needs a C compiler)
#   desktop/build.sh all
# CC / CLANG choose the compilers, CFLAGS_EXTRA adds flags (e.g. "-arch x86_64 -arch arm64" on a Mac).
# The engine itself is NOT in this directory: it is compiled straight from modules/filmlook (see src/fd_engine.c).
set -euo pipefail
cd "$(dirname "$0")"
OUT=out
SRC=src
WARN="-Wall -Wextra -Wno-unused-function -Wno-unused-parameter -ffp-contract=off"
mkdir -p "$OUT"

build_web() {
  local clang="${CLANG:-clang}"
  "$clang" --target=wasm32 -O2 -mcpu=mvp -ffreestanding -nostdlib -DFD_WASM -std=gnu99 $WARN \
    -Wl,--no-entry -Wl,--export-memory -Wl,-z,stack-size=1048576 -Wl,--strip-all \
    -o "$OUT/fd_engine.wasm" "$SRC/fd_wasm.c" "$SRC/fd_engine.c"
  python3 build_web.py "$OUT/fd_engine.wasm" "$OUT/FILMLOOK_DESKTOP.html"
}

build_cli() {
  local cc="${CC:-cc}" exe="filmlook_cli" extra=""
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) exe="filmlook_cli.exe"; extra="-static" ;;
  esac
  # shellcheck disable=SC2086
  "$cc" -O2 -std=gnu99 $WARN ${CFLAGS_EXTRA:-} $extra -o "$OUT/$exe" "$SRC/fd_cli.c" "$SRC/fd_engine.c" $( [ "$exe" = filmlook_cli.exe ] && echo -lshell32 )
}

case "${1:-all}" in
  web) build_web ;;
  cli) build_cli ;;
  all) build_web; build_cli ;;
  *) echo "usage: $0 web|cli|all"; exit 2 ;;
esac
