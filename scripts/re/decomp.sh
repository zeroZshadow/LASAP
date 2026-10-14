#!/bin/bash
# Decompiles functions of the game's main module with Ghidra headless, named from las.sym:
#   scripts/re/decomp.sh <out.c> <hex module offset>...
# LASAP_RE (default D:/DEV/LASAP-sandbox/re) holds main.bin (DBG dumpmain); the Ghidra project is made there
# (ghidra/) on first use, without auto-analysis. GHIDRA_HOME defaults to scoop's ghidra. The project is opened
# read-only: names and functions apply for the run only.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
RE="${LASAP_RE:-D:/DEV/LASAP-sandbox/re}"
GHIDRA="${GHIDRA_HOME:-$HOME/scoop/apps/ghidra/current}"
win() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
out="$1"
shift
[ -n "$out" ] && [ $# -gt 0 ] || { echo "usage: decomp.sh <out.c> <hex offset>..." >&2; exit 2; }
mkdir -p "$RE/ghidra"
rm -f "$out"
log="$RE/ghidra/last.log"
if [ ! -d "$RE/ghidra/las.rep" ]; then
  "$GHIDRA/support/analyzeHeadless.bat" "$(win "$RE/ghidra")" las -import "$(win "$RE/main.bin")" -noanalysis \
    -processor "AARCH64:LE:64:v8A" -loader BinaryLoader -loader-baseAddr 0x8504000 > "$log" 2>&1
fi
"$GHIDRA/support/analyzeHeadless.bat" "$(win "$RE/ghidra")" las -process main.bin -noanalysis -readOnly \
  -scriptPath "$(win "$HERE/ghidra")" -preScript ImportSymbols.java "$(win "$HERE/las.sym")" \
  -postScript Decomp.java "$(win "$out")" "$@" > "$log" 2>&1
grep -E "ERROR|Exception|no function" "$log" | head -5 || true
cat "$out"
