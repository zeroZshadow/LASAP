#!/bin/bash
# Builds the exlaunch module in Docker: the release module into patcher/exl (what the patcher installs and the
# release ships) and the test build (LASAP_TEST: DBG commands, the debug port, the trace) into patcher/exl-test,
# which deploy_mod.sh and reload_exl.sh install in the sandbox. --release builds the release module only (the release
# workflow).
set -e
# devkitA64 by digest, so every build (and the release workflow, which runs this script) uses the same toolchain
IMAGE=devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282
RELEASE_ONLY=0
if [ "$1" = "--release" ]; then RELEASE_ONLY=1; fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINROOT="$(cygpath -m "$ROOT" 2>/dev/null || echo "$ROOT")"
MSYS_NO_PATHCONV=1 docker run --rm -e RELEASE_ONLY="$RELEASE_ONLY" -v "$WINROOT/las-ap-exl:/src" -w /src "$IMAGE" bash -c '
    set -e
    build() {
        rm -f deploy/subsdk9 deploy/main.npdm src.elf src.nso src.npdm
        make -j16 "$@" 2>&1 | grep -E "error|warning" || true
        test -f deploy/subsdk9
    }
    build
    # The release module must carry none of the test code (the .nso is compressed; the .elf is not).
    if grep -aq "debug port" src.elf; then echo "the release module contains test code"; exit 1; fi
    mkdir -p out/release && cp deploy/subsdk9 deploy/main.npdm out/release/
    if [ "$RELEASE_ONLY" = 1 ]; then exit 0; fi
    build BUILD=build-test DEFINES=-DLASAP_TEST
    grep -aq "debug port" src.elf
    mkdir -p out/test && cp deploy/subsdk9 deploy/main.npdm out/test/'
mkdir -p "$ROOT/patcher/exl"
cp "$ROOT/las-ap-exl/out/release/"* "$ROOT/patcher/exl/"
if [ "$RELEASE_ONLY" = 1 ]; then
    echo "built patcher/exl/subsdk9 (release)"
    exit 0
fi
mkdir -p "$ROOT/patcher/exl-test"
cp "$ROOT/las-ap-exl/out/test/"* "$ROOT/patcher/exl-test/"
echo "built patcher/exl/subsdk9 (release) and patcher/exl-test/subsdk9 (test)"
