#!/bin/sh
# Try merging QEMU's master into LEMU's master, in memory only.
# It changes no branch, file, index or worktree. It fetches
# upstream/master first; -n skips the fetch.
#
# usage: check.sh [-n] [OURS [THEIRS]]    default: master upstream/master
# exit:  0 clean or nothing new, 1 conflicts, 2 error

fetch=yes
if [ "${1-}" = -n ]; then
    fetch=no
    shift
fi
ours=${1:-master}
theirs=${2:-upstream/master}

die() {
    echo "check.sh: $*" >&2
    exit 2
}

# indent each line, or say "none"
show() {
    sed 's/^/  /' | grep . || echo "  none"
}

git rev-parse --git-dir >/dev/null 2>&1 || die "run it inside a LEMU checkout"
if [ $fetch = yes ]; then
    git fetch --quiet upstream master || die "git fetch upstream master failed"
fi
base=$(git merge-base "$ours" "$theirs") || die "$ours and $theirs have no common commit"

new=$(git rev-list --count "$base..$theirs")
echo "QEMU: $new new commits, $(git rev-parse --short "$base")..$(git rev-parse --short "$theirs") (latest $(git log -1 --format=%cs "$theirs"))"
echo "LEMU: $(git rev-list --count --no-merges "$base..$ours") commits of its own"
if [ "$new" -eq 0 ]; then
    echo "Nothing new to merge."
    exit 0
fi

tmp=$(mktemp -d) || die "mktemp failed"
trap 'rm -rf "$tmp"' EXIT
trap 'exit 2' HUP INT TERM

git merge-tree --write-tree --name-only --messages "$ours" "$theirs" >"$tmp/merge" 2>&1
rc=$?
echo
case $rc in
0)  echo "Merge: clean" ;;
1)  echo "Merge: CONFLICTS"
    grep '^CONFLICT' "$tmp/merge" | show ;;
*)  sed 's/^/  /' "$tmp/merge" >&2
    die "git merge-tree failed (it needs git 2.38 or newer)" ;;
esac

git diff --name-only "$base" "$ours" | LC_ALL=C sort >"$tmp/ours"
git diff --name-only "$base" "$theirs" | LC_ALL=C sort >"$tmp/theirs"
git diff --name-only --diff-filter=MD "$base" "$ours" >"$tmp/changed"
git diff --name-only --diff-filter=D "$base" "$ours" >"$tmp/deleted"

echo
echo "Files both sides changed:"
LC_ALL=C comm -12 "$tmp/ours" "$tmp/theirs" | show

# the new QEMU commits that touch the paths listed in file $1
touching() {
    if [ -s "$1" ]; then
        # QEMU paths have no spaces, so plain word splitting is fine
        # shellcheck disable=SC2046
        git log --oneline --no-merges "$base..$theirs" -- $(cat "$1") | show
    else
        echo "  none"
    fi
}
echo
echo "QEMU commits that touch QEMU files LEMU changed or deleted:"
touching "$tmp/changed"
echo
echo "Of those, the ones on files LEMU deleted (port them by hand):"
touching "$tmp/deleted"

echo
echo "Files QEMU deleted:"
git diff --name-only --diff-filter=D "$base" "$theirs" | show

echo
echo "New in QEMU's deprecated and removed lists (docs/about):"
git diff -U0 "$base" "$theirs" -- docs/about/deprecated.rst docs/about/removed-features.rst |
    grep -E '^\+.*\((since|removed in) [0-9]' | sed 's/^+//' | show

exit $rc
