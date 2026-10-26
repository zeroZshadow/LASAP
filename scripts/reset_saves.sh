#!/bin/bash
# Restores the sandbox's save data and clears the module's checks files.
#   reset_saves.sh           from a copy of the user's real Ryujinx saves (read-only on their side)
#   reset_saves.sh newgame   from the sandbox folder's save-newgame: file 2 created (name confirmed) but never played
set -e
PY="${LASAP_PY:-$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)/../.venv-lasr/Scripts/python.exe}"
"$PY" "$(dirname "$0")/emu.py" check
SANDBOX="$("$PY" "$(dirname "$0")/local_paths.py" sandbox)"
case "$1" in
    newgame) SRC="$SANDBOX/save-newgame" ;;
    "") SRC="$APPDATA/Ryujinx/bis/user/save" ;;
    *) echo "usage: reset_saves.sh [newgame]"; exit 1 ;;
esac
DST="$SANDBOX/ryujinx/bis/user/save"
rm -rf "$DST" && cp -r "$SRC" "$DST"
rm -rf "$SANDBOX/ryujinx/sdcard/lasap"
echo "saves reset${1:+ ($1)}"
