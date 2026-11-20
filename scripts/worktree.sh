#!/bin/bash
# Work in a git worktree of this repository beside other sessions (TESTING.md, "Working in a worktree").
#   worktree.sh setup      own Archipelago worktree (vendor/Archipelago, worlds/las -> this worktree's world, Universal
#                          Tracker), the built modules (patcher/exl, patcher/exl-test, with their build times) and
#                          scripts/local_paths.json from the main checkout
#   worktree.sh test [args] the world's tests with pytest, on this worktree's world: the test paths given
#                          (worlds/las/test/...), else the whole folder; each skip prints its reason
#   worktree.sh land <subject> [<line> [<line>]]
#                          rebase this branch onto master, run the whole world test suite, put the branch on master as
#                          one commit with that message, then remove the Archipelago worktree; refuses while a process
#                          has vendor/Archipelago open
#   worktree.sh teardown   remove the Archipelago worktree only
# The venvs stay in the main checkout ($MAIN/.venv-ap, $MAIN/.venv-lasr); nothing here links to them.
set -e
ROOT="$(git rev-parse --show-toplevel)"
MAIN="$(cd "$(git rev-parse --git-common-dir)/.." && pwd)"
AP="$ROOT/vendor/Archipelago"
[ "$ROOT" != "$MAIN" ] || { echo "run this in a worktree, not in the main checkout ($MAIN)"; exit 1; }
winpath() { cygpath -w "$1"; }

teardown() {
    # Remove the junction first, so nothing deletes through it into this worktree's world; stop if it is still there.
    if [ -e "$AP/worlds/las" ]; then
        cmd //c rmdir "$(winpath "$AP/worlds/las")" || return 1
        [ ! -e "$AP/worlds/las" ] || return 1
    fi
    if [ -e "$AP" ]; then git -C "$MAIN/vendor/Archipelago" worktree remove --force "$AP" || return 1; fi
    git -C "$MAIN/vendor/Archipelago" worktree prune
}

# Whether a process has vendor/Archipelago or something in it open (a server, a test run or a shell started there):
# Windows refuses to rename a folder then
in_use() {
    [ -e "$AP" ] || return 1
    if mv "$AP" "$AP.in-use-check" 2>/dev/null; then
        mv "$AP.in-use-check" "$AP"
        return 1
    fi
    return 0
}
IN_USE="vendor/Archipelago is open in another process (a server, test run or shell started in it, or one that reads the world through it): stop that process"

run_tests() {
    # The whole folder unless a test path is given
    local paths=(worlds/las/test) arg
    for arg in "$@"; do
        case "$arg" in worlds/las/test*) paths=() ;; esac
    done
    [ -e "$AP/worlds/las" ] || { echo "no Archipelago worktree: run worktree.sh setup"; return 1; }
    (cd "$AP" && "$MAIN/.venv-ap/Scripts/python.exe" -m pytest "${paths[@]}" -q -rs "$@")  # -rs: each skip's reason
}

case "$1" in
setup)
    if [ -e "$AP" ] && [ ! -e "$AP/.git" ]; then  # what a failed teardown left
        in_use && { echo "$IN_USE, then run setup again"; exit 1; }
        [ ! -e "$AP/worlds/las" ] || { echo "vendor/Archipelago is no Archipelago worktree but links the world: run worktree.sh teardown"; exit 1; }
        rm -rf "$AP"
    fi
    if [ ! -e "$AP" ]; then
        git -C "$MAIN/vendor/Archipelago" worktree add --detach "$AP" HEAD
        mkdir -p "$AP/Players"
    fi
    # Universal Tracker, which the client's tests need (TESTING.md, Requirements); a skipped test says it is missing
    if [ ! -e "$AP/custom_worlds/tracker.apworld" ] && [ -e "$MAIN/vendor/Archipelago/custom_worlds/tracker.apworld" ]; then
        mkdir -p "$AP/custom_worlds"
        cp "$MAIN/vendor/Archipelago/custom_worlds/tracker.apworld" "$AP/custom_worlds/"
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
            cp -p "$MAIN/patcher/$dir/"* "$ROOT/patcher/$dir/"  # with their times: the staleness checks read them
        fi
    done
    echo "worktree ready: Archipelago at vendor/Archipelago, venvs at $MAIN/.venv-ap and $MAIN/.venv-lasr"
    ;;
test)
    shift
    run_tests "$@"
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
    # Landing removes the Archipelago worktree at the end, which fails while a process has it open
    in_use && { echo "not landed: $IN_USE, then land again"; exit 1; }
    # The whole suite, not only the tests of the files changed: a change to data such as logic.yml breaks tests elsewhere
    if ! run_tests -p no:warnings; then
        echo "not landed: the world's tests fail. Fix them, commit, land again"
        exit 1
    fi
    in_use && { echo "not landed: $IN_USE, then land again"; exit 1; }
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
    echo "landed $BRANCH on master as $(git rev-parse --short "$SQUASH")"
    if ! teardown; then
        echo "the Archipelago worktree is not removed: $IN_USE, then run worktree.sh teardown"
        exit 1
    fi
    ;;
teardown)
    if ! teardown; then
        echo "not removed: $IN_USE, then run teardown again"
        exit 1
    fi
    ;;
*)
    sed -n '2,13p' "$0"
    exit 1
    ;;
esac
