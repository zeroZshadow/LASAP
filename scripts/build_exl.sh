#!/bin/bash
# Builds the exlaunch module in Docker and copies it into patcher/exl.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINROOT="$(cygpath -m "$ROOT" 2>/dev/null || echo "$ROOT")"
MSYS_NO_PATHCONV=1 docker run --rm -v "$WINROOT/las-ap-exl:/src" -w /src devkitpro/devkita64:latest \
    bash -c 'make -j16 2>&1 | grep -E "error|warning" ; test -f deploy/subsdk9'
mkdir -p "$ROOT/patcher/exl"
cp "$ROOT/las-ap-exl/deploy/subsdk9" "$ROOT/las-ap-exl/deploy/main.npdm" "$ROOT/patcher/exl/"
echo "built patcher/exl/subsdk9"
