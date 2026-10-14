#!/bin/bash
# Restarts the sandbox game with the freshly built test module (patcher/exl-test/subsdk9) and boots to file 1.
# Needs the scripted client running (TESTING.md, "Scripted AP session").
set -e
cd "$(dirname "$0")/.."
# In a worktree the venvs stay in the main checkout.
PY=.venv-ap/Scripts/python.exe
[ -x "$PY" ] || PY="$(git rev-parse --git-common-dir)/../.venv-ap/Scripts/python.exe"
$PY scripts/emu.py check
$PY scripts/emu.py stop || true
cp patcher/exl-test/subsdk9 /d/DEV/LASAP-sandbox/ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000/exefs/subsdk9
$PY scripts/emu.py start
$PY scripts/e2e.py run scripts/tests/boot_file1.txt
$PY scripts/e2e.py state
