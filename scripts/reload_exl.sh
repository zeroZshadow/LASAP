#!/bin/bash
# Restarts the sandbox game with the freshly built test module (patcher/exl-test/subsdk9) on the new-game
# snapshot's saves (reset_saves.sh) and boots to a new game in file 2.
set -e
cd "$(dirname "$0")/.."
# In a worktree the venvs stay in the main checkout.
PY=.venv-ap/Scripts/python.exe
[ -x "$PY" ] || PY="$(git rev-parse --git-common-dir)/../.venv-ap/Scripts/python.exe"
$PY patcher/module_check.py patcher/exl-test/subsdk9
$PY scripts/emu.py check
$PY scripts/emu.py stop || true
cp patcher/exl-test/subsdk9 "$($PY scripts/local_paths.py sandbox)"/ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000/exefs/subsdk9
LASAP_PY="$PY" bash scripts/reset_saves.sh
$PY scripts/emu.py start
$PY scripts/e2e.py run scripts/tests/newgame_file2.txt
$PY scripts/e2e.py state
