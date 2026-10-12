#!/bin/bash
# Restarts the sandbox game with the freshly built module (patcher/exl/subsdk9) and boots to file 1.
# Needs the scripted client running (TESTING.md, "Scripted AP session").
set -e
cd "$(dirname "$0")/.."
PY=.venv-ap/Scripts/python.exe
$PY scripts/emu.py stop || true
cp patcher/exl/subsdk9 /d/DEV/LASAP-sandbox/ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000/exefs/subsdk9
$PY scripts/emu.py start
$PY scripts/e2e.py run scripts/tests/boot_file1.txt
$PY scripts/e2e.py state
