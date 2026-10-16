#!/usr/bin/env bash
# Author a platform patch for projects/bw2/remap/patches/ (see remap_runtime.py).
#   patchwork.sh start FILE...   copy the upstream text (with the existing patches) of FILEs
#                                into build/bw2-patchwork/{a,b}; edit the copies under b/
#   patchwork.sh diff NAME       write the b/ edits as projects/bw2/remap/patches/NAME.patch
# Then run remap_runtime.py (or iterate.sh) to apply them. start copies the text with
# every current patch applied, so it suits a new, last patch; to redo an existing one,
# delete it and run remap_runtime.py first, or the new diff repeats its hunks.
set -euo pipefail
cd "$(dirname "$0")/../../.."
W=build/bw2-patchwork
UPSTREAM=05e82a3
case "${1:-}" in
start)
    shift
    for f in "$@"; do
        mkdir -p "$W/a/$(dirname "$f")" "$W/b/$(dirname "$f")"
        if [ -f "build/bw2-remap/patched-upstream/$f" ]; then
            cp "build/bw2-remap/patched-upstream/$f" "$W/a/$f"
        else
            git show "$UPSTREAM:$f" > "$W/a/$f"
        fi
        [ -f "$W/b/$f" ] || cp "$W/a/$f" "$W/b/$f"
    done ;;
diff)
    out="projects/bw2/remap/patches/$2.patch"
    (cd "$W" && diff -ruN a b) > "$out" || true
    echo "wrote $out ($(grep -c '^@@' "$out") hunks)"
    rm -rf "$W" ;;
*)
    sed -n '2,6p' "$0"; exit 1 ;;
esac
