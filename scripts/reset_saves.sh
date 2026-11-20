#!/bin/bash
# Restores the sandbox's save data from the new-game snapshot (the sandbox folder's save-newgame, built by
# newgame_save.py: file 2 created but never played, no other file) and clears the module's checks files.
set -e
PY="${LASAP_PY:-$(git -C "$(dirname "$0")" rev-parse --path-format=absolute --git-common-dir)/../.venv-lasr/Scripts/python.exe}"
"$PY" "$(dirname "$0")/emu.py" check
[ $# -eq 0 ] || { echo "usage: reset_saves.sh"; exit 1; }
SANDBOX="$("$PY" "$(dirname "$0")/local_paths.py" sandbox)"
DST="$SANDBOX/ryujinx/bis/user/save"
rm -rf "$DST" && cp -r "$SANDBOX/save-newgame" "$DST"
rm -rf "$SANDBOX/ryujinx/sdcard/lasap"
echo "saves reset"
