#!/bin/bash
# Installs a patcher output folder (SD layout) into the sandbox emulator: deploy_mod.sh <out dir>
# The sandbox runs the test build of the module (patcher/exl-test, scripts/build_exl.sh) in place of the patcher's.
set -e
TEST_MODULE="$(cd "$(dirname "$0")/.." && pwd)/patcher/exl-test/subsdk9"
[ -f "$TEST_MODULE" ] || { echo "no test module: run scripts/build_exl.sh"; exit 1; }
"${LASAP_PY:-/d/DEV/LASAP/.venv-lasr/Scripts/python.exe}" "$(dirname "$0")/emu.py" check
SB=/d/DEV/LASAP-sandbox/ryujinx/sdcard
OUT="$1"
rm -rf "$SB/atmosphere/contents/01006BB00C6F0000"
mkdir -p "$SB/atmosphere/contents" "$SB/config/lasr-exl"
rm -f "$SB/config/lasr-exl/ap_map.txt"
cp -r "$OUT/atmosphere/contents/01006BB00C6F0000" "$SB/atmosphere/contents/"
cp "$TEST_MODULE" "$SB/atmosphere/contents/01006BB00C6F0000/exefs/subsdk9"
cp "$OUT/config/lasr-exl/"* "$SB/config/lasr-exl/"
# The sandbox game listens on its own port, so it can run beside a modded game on the default port.
sed -i "/^seed_hash/a port = ${LASAP_GAME_PORT:-43385}" "$SB/config/lasr-exl/config.ini"
echo "deployed $OUT"
