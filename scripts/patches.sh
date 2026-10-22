#!/usr/bin/env bash
# Helpers for maintaining the libprojectM patch set in third_party/patches/.
#
#   scripts/patches.sh start <dir>
#       Creates a git worktree of the pinned projectM submodule at <dir> and applies the
#       existing patches as commits. Edit and commit there (one concern per commit).
#
#   scripts/patches.sh export <dir> <short-name>
#       Exports the newest commit in <dir> as the next numbered patch,
#       e.g. third_party/patches/0002-<short-name>.patch.
#
#   scripts/patches.sh finish <dir>
#       Removes the worktree.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
submodule="$repo_root/third_party/projectm"
patch_dir="$repo_root/third_party/patches"

usage() {
    sed -n '2,15p' "$0"
    exit 1
}

[[ $# -ge 2 ]] || usage
command="$1"
work_dir="$2"

case "$command" in
    start)
        [[ ! -e "$work_dir" ]] || { echo "error: $work_dir already exists" >&2; exit 1; }
        pinned="$(git -C "$submodule" rev-parse HEAD)"
        git -C "$submodule" worktree add --quiet --detach "$work_dir" "$pinned"
        shopt -s nullglob
        patches=("$patch_dir"/*.patch)
        if [[ ${#patches[@]} -gt 0 ]]; then
            git -C "$work_dir" am --quiet "${patches[@]}"
        fi
        echo "Worktree ready at $work_dir with ${#patches[@]} patch(es) applied on $(git -C "$submodule" describe --tags)."
        ;;
    export)
        [[ $# -eq 3 ]] || usage
        name="$3"
        count=$(find "$patch_dir" -maxdepth 1 -name '*.patch' | wc -l | tr -d ' ')
        number=$(printf '%04d' $((count + 1)))
        out="$patch_dir/$number-$name.patch"
        git -C "$work_dir" format-patch --quiet -1 --stdout > "$out"
        echo "Wrote $out"
        ;;
    finish)
        git -C "$submodule" worktree remove "$work_dir"
        ;;
    *)
        usage
        ;;
esac
