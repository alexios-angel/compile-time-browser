#!/usr/bin/env bash
# Format the sources, or check that they are already formatted.
#
#   tools/format.sh          rewrite every file in place
#   tools/format.sh --check  exit non-zero and print the diff, for CI
#
# The file list is `git ls-files`, so a file that is not tracked is not
# formatted and a build directory cannot be picked up by accident. What NOT to
# format is in .clang-format-ignore rather than here: one list, and clang-format
# honours it whether it is invoked through this script or by an editor.
set -euo pipefail

cd "$(dirname "$0")/.."

# The project's own clang-format, so a run here and a run in CI agree. Falls
# back to whatever is on PATH, which is fine for a check but can differ by a
# version.
format=tools/clang-std-embed/bin/clang-format
if [[ ! -x $format ]]; then
    format=$(command -v clang-format || true)
fi
if [[ -z $format ]]; then
    echo "format.sh: no clang-format found (expected tools/clang-std-embed/bin/clang-format)" >&2
    exit 127
fi
echo "format.sh: using $("$format" --version)"

mapfile -t files < <(git ls-files '*.cpp' '*.cppm' '*.hpp' '*.h' | grep -v '^external/' | grep -v '^build')
if [[ ${#files[@]} -eq 0 ]]; then
    echo "format.sh: nothing to format" >&2
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
