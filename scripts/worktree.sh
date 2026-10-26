#!/bin/bash
# Work in a git worktree of this repository beside other sessions (TESTING.md, "Working in a worktree").
#   worktree.sh setup      own Archipelago worktree (vendor/Archipelago, worlds/las -> this worktree's world) and the
#                          built modules (patcher/exl, patcher/exl-test) and scripts/local_paths.json from the main
#                          checkout
#   worktree.sh test [args] the world's tests with pytest, on this worktree's world
#   worktree.sh land <subject> [<line> [<line>]]
#                          rebase this branch onto master, put it on master as one commit with that message, then
#                          remove the Archipelago worktree
#   worktree.sh teardown   remove the Archipelago worktree only
# The venvs stay in the main checkout ($MAIN/.venv-ap, $MAIN/.venv-lasr); nothing here links to them.
set -e
ROOT="$(git rev-parse --show-toplevel)"
MAIN="$(cd "$(git rev-parse --git-common-dir)/.." && pwd)"
AP="$ROOT/vendor/Archipelago"
[ "$ROOT" != "$MAIN" ] || { echo "run this in a worktree, not in the main checkout ($MAIN)"; exit 1; }
winpath() { cygpath -w "$1"; }

teardown() {
    # Remove the junction first, so nothing deletes through it into this worktree's world.
    if [ -e "$AP/worlds/las" ]; then cmd //c rmdir "$(winpath "$AP/worlds/las")"; fi
    if [ -e "$AP" ]; then git -C "$MAIN/vendor/Archipelago" worktree remove --force "$AP"; fi
    git -C "$MAIN/vendor/Archipelago" worktree prune
}

case "$1" in
setup)
    if [ ! -e "$AP" ]; then
        git -C "$MAIN/vendor/Archipelago" worktree add --detach "$AP" HEAD
        mkdir -p "$AP/Players"
    fi
    if [ ! -e "$AP/worlds/las" ]; then
        cmd //c mklink //J "$(winpath "$AP/worlds/las")" "$(winpath "$ROOT/apworld/las")" > /dev/null
    fi
    if [ ! -e "$ROOT/scripts/local_paths.json" ] && [ -e "$MAIN/scripts/local_paths.json" ]; then
        cp "$MAIN/scripts/local_paths.json" "$ROOT/scripts/"
    fi
    for dir in exl exl-test; do
        if [ ! -e "$ROOT/patcher/$dir/subsdk9" ] && [ -e "$MAIN/patcher/$dir/subsdk9" ]; then
            mkdir -p "$ROOT/patcher/$dir"
            cp "$MAIN/patcher/$dir/"* "$ROOT/patcher/$dir/"
        fi
    done
    echo "worktree ready: Archipelago at vendor/Archipelago, venvs at $MAIN/.venv-ap and $MAIN/.venv-lasr"
    ;;
test)
    shift
    cd "$AP" && "$MAIN/.venv-ap/Scripts/python.exe" -m pytest worlds/las/test -q "$@"
    ;;
land)
    shift
    [ $# -ge 1 ] && [ $# -le 3 ] && [ -n "$1" ] || { echo "usage: worktree.sh land <subject> [<line> [<line>]]"; exit 1; }
    BRANCH="$(git branch --show-current)"
    [ -n "$BRANCH" ] || { echo "not on a branch"; exit 1; }
    if [ -n "$(git status --porcelain)" ]; then echo "commit or remove uncommitted changes first"; exit 1; fi
    [ "$(git -C "$MAIN" branch --show-current)" = master ] || { echo "the main checkout is not on master"; exit 1; }
    if ! git merge-base --is-ancestor master HEAD; then
        if ! git rebase master; then
            echo "rebase onto master stopped on conflicts: resolve them, git rebase --continue, rerun the tests, land again"
            exit 1
        fi
        echo "rebased onto master: rerun the tests, then land again"
        exit 1
    fi
    [ "$(git rev-parse HEAD)" != "$(git rev-parse master)" ] || { echo "nothing to land: this branch is master"; exit 1; }
    # The squash is made here, not in the main checkout, whose index other sessions may be using; master then only
    # fast-forwards to it.
    if [ $# -gt 1 ]; then
        SQUASH="$(git commit-tree "HEAD^{tree}" -p master -m "$1" -m "$(printf '%s\n' "${@:2}")")"
    else
        SQUASH="$(git commit-tree "HEAD^{tree}" -p master -m "$1")"
    fi
    if ! git -C "$MAIN" merge --ff-only "$SQUASH"; then
        echo "not landed: the main checkout has uncommitted edits in files this branch changes (see above)."
        echo "Another session is working there; wait until it has committed them, then land again."
        exit 1
    fi
    git reset -q --hard "$SQUASH"
    teardown
    echo "landed $BRANCH on master as $(git rev-parse --short "$SQUASH")"
    ;;
teardown)
    teardown
    ;;
*)
    sed -n '2,11p' "$0"
    exit 1
    ;;
esac
