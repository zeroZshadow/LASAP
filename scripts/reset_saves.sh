#!/bin/bash
# Restores the sandbox's save data from a copy of the user's real Ryujinx saves (read-only on their side).
set -e
SRC="$APPDATA/Ryujinx/bis/user/save"
DST=/d/DEV/LASAP-sandbox/ryujinx/bis/user/save
rm -rf "$DST" && cp -r "$SRC" "$DST"
rm -rf /d/DEV/LASAP-sandbox/ryujinx/sdcard/lasap
echo "saves reset"
