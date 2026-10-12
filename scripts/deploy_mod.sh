#!/bin/bash
# Installs a patcher output folder (SD layout) into the sandbox emulator: deploy_mod.sh <out dir>
set -e
SB=/d/DEV/LASAP-sandbox/ryujinx/sdcard
OUT="$1"
rm -rf "$SB/atmosphere/contents/01006BB00C6F0000"
mkdir -p "$SB/atmosphere/contents" "$SB/config/lasr-exl"
cp -r "$OUT/atmosphere/contents/01006BB00C6F0000" "$SB/atmosphere/contents/"
cp "$OUT/config/lasr-exl/"* "$SB/config/lasr-exl/"
# The sandbox game listens on its own port, so it can run beside a modded game on the default port.
sed -i "/^seed_hash/a port = ${LASAP_GAME_PORT:-43385}" "$SB/config/lasr-exl/config.ini"
echo "deployed $OUT"
