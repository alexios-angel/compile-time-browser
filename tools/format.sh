#!/usr/bin/env bash
# Format the sources, or check that they are already formatted.
#
#   tools/format.sh          rewrite every file in place
#   tools/format.sh --check  exit non-zero and print the diff
#
# The file list is `git ls-files`, tracked AND untracked-but-not-ignored, so a
# build directory still cannot be picked up by accident - build/ is gitignored,
# and --exclude-standard honours that.
#
# TRACKED-ONLY WAS A HOLE, and it was found the way these things are: an agent
# added two new files, ran --check, was told the tree was clean, and committed
# both unformatted. A gate that cannot see a file nobody has `git add`ed yet is
# a gate that passes precisely when a new file is at its least reviewed. It covers
# the WHOLE monorepo - ctbrowser/ and ctcompile/ both - because there is one
# .clang-format and one house style, and a second formatter invocation is a
# second chance to forget one. What NOT to
# format is in .clang-format-ignore rather than here: one list, and clang-format
# honours it whether it is invoked through this script or by an editor.
set -euo pipefail

cd "$(dirname "$0")/.."

# The project's own clang-format, so a run here and a run on the devbox agree.
# Falls back to whatever is on PATH, which is fine for a check but can differ by
# a version.
format=tools/clang-std-embed/bin/clang-format
if [[ ! -x $format ]]; then
    format=$(command -v clang-format || true)
fi
if [[ -z $format ]]; then
    echo "format.sh: no clang-format found (expected tools/clang-std-embed/bin/clang-format)" >&2
    exit 127
fi
echo "format.sh: using $("$format" --version)"

mapfile -t files < <({ git ls-files '*.cpp' '*.cppm' '*.hpp' '*.h'
                       git ls-files --others --exclude-standard \
                                    '*.cpp' '*.cppm' '*.hpp' '*.h'; } \
                     | grep -v '^third-party/' | grep -v '^build')
if [[ ${#files[@]} -eq 0 ]]; then
    # WHICH KIND OF NOTHING, because the two have different fixes and the old
    # message covered both. `git ls-files` failing is the common one for an
    # agent working in a worktree synced to another machine: the .git there is a
    # POINTER FILE whose target does not exist on that box, so git errors, the
    # list comes back empty, and the script exits 1 having formatted nothing.
    # Chained as `format.sh && remote-build.sh` that reads as the build
    # mysteriously not running.
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "format.sh: this is not a usable git repository - git ls-files cannot" >&2
        echo "  run, so NOTHING was formatted. In a worktree synced to another" >&2
        echo "  machine, .git is a pointer file whose target is not there." >&2
        exit 2
    fi
    echo "format.sh: no source files matched" >&2
    exit 1
fi

if [[ ${1:-} == "--check" ]]; then
    failed=0
    for file in "${files[@]}"; do
        # An ignored file formats to nothing; skip it rather than reporting the
        # whole file as a diff.
        formatted=$("$format" "$file") || continue
        [[ -z $formatted ]] && continue
        if ! diff -u --label "$file" --label "$file (formatted)" "$file" <(printf '%s\n' "$formatted"); then
            failed=1
        fi
    done
    if [[ $failed -ne 0 ]]; then
        echo >&2
        echo "format.sh: the files above are not formatted. Run tools/format.sh" >&2
        exit 1
    fi
    echo "format.sh: ${#files[@]} files are formatted"
    exit 0
fi

"$format" -i "${files[@]}"
echo "format.sh: formatted ${#files[@]} files"
