#!/bin/bash
# Restores the sandbox's save data and clears the module's checks files.
#   reset_saves.sh           from a copy of the user's real Ryujinx saves (read-only on their side)
#   reset_saves.sh newgame   from D:\DEV\LASAP-sandbox\save-newgame: file 2 created (name confirmed) but never played
set -e
"${LASAP_PY:-/d/DEV/LASAP/.venv-lasr/Scripts/python.exe}" "$(dirname "$0")/emu.py" check
case "$1" in
    newgame) SRC=/d/DEV/LASAP-sandbox/save-newgame ;;
    "") SRC="$APPDATA/Ryujinx/bis/user/save" ;;
    *) echo "usage: reset_saves.sh [newgame]"; exit 1 ;;
esac
DST=/d/DEV/LASAP-sandbox/ryujinx/bis/user/save
rm -rf "$DST" && cp -r "$SRC" "$DST"
rm -rf /d/DEV/LASAP-sandbox/ryujinx/sdcard/lasap
echo "saves reset${1:+ ($1)}"
