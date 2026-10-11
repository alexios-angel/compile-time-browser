#!/usr/bin/env bash
# Format the sources, or check that they are already formatted.
#
#   tools/format.sh          rewrite every file in place
#   tools/format.sh --check  exit non-zero and list what is not formatted
#
# The file list is `git ls-files`, tracked and untracked-but-not-ignored, so a
# build directory still cannot be picked up by accident: build/ is gitignored,
# and --exclude-standard honours that.
#
# Untracked files are included on purpose: a new file not yet `git add`ed is
# the one most likely to be unformatted. The script covers the whole monorepo
# (ctbrowser/ and ctcompile/) because there is one .clang-format. Files to
# skip are listed in .clang-format-ignore, which clang-format honours whether
# it runs from here or from an editor.
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
    # Tell the two kinds of empty list apart, because they have different
    # fixes. `git ls-files` fails in a worktree synced to another machine: the
    # .git there is a pointer file whose target does not exist on that machine,
    # so git errors and the list comes back empty.
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "format.sh: this is not a usable git repository - git ls-files cannot" >&2
        echo "  run, so NOTHING was formatted. In a worktree synced to another" >&2
        echo "  machine, .git is a pointer file whose target is not there." >&2
        exit 2
    fi
    echo "format.sh: no source files matched" >&2
    exit 1
fi

# Python: `ruff format` (configured in pyproject.toml) over the same
# tracked-or-untracked list. uv runs the ruff uv.lock pins, from the dev group it
# installs by default; without uv, the ruff on PATH (tools/Brewfile). A missing
# ruff is an error, not a skip, so a check never passes by formatting nothing.
if command -v uv >/dev/null; then
    ruff=(uv run --quiet --frozen ruff)
elif command -v ruff >/dev/null; then
    ruff=(ruff)
else
    echo "format.sh: no uv or ruff found - brew install uv (tools/Brewfile)" >&2
    exit 127
fi
mapfile -t pyfiles < <({ git ls-files '*.py'
                         git ls-files --others --exclude-standard '*.py'; }                        | grep -v '^third-party/' | grep -v '^build')

# Web files: js-beautify (the npm package, `npm install -g js-beautify`;
# brew's is the Python port, which has no html-beautify) over the hand-written
# JavaScript, HTML and CSS, configured by .jsbeautifyrc at the root. vendor/ is
# third-party code and is skipped. Test fixtures and pages are formatted,
# ctbrowser/test/ and ctcompile/test/ included: the drivers hash inline
# sources, not these files, and block-level indentation is whitespace the
# engine collapses (the golden-image render tests confirm this).
html_beautify=$(command -v html-beautify || true)
if [[ -z $html_beautify ]]; then
    echo "format.sh: no html-beautify found - npm install -g js-beautify" >&2
    exit 127
fi
beautify_bin=$(dirname "$html_beautify")
mapfile -t webfiles < <({ git ls-files '*.js' '*.html' '*.css'
                          git ls-files --others --exclude-standard '*.js' '*.html' '*.css'; } \
                        | grep -v '^third-party/' | grep -v '^build' | grep -v '/vendor/')
# One process per file: the CLI has no --check, so a check is "does the
# output equal the input", and --replace is the same run writing back.
beautify() {  # beautify <check|replace> <file>...
    local mode=$1 rc=0 f tool; shift
    for f in "$@"; do
        case $f in *.js) tool=js-beautify;; *.css) tool=css-beautify;; *) tool=html-beautify;; esac
        if [[ $mode == check ]]; then
            "$beautify_bin/$tool" --config .jsbeautifyrc "$f" | cmp -s - "$f" || { echo "$f: not formatted" >&2; rc=1; }
        else
            "$beautify_bin/$tool" --config .jsbeautifyrc --quiet --replace "$f"
        fi
    done
    return $rc
}

if [[ ${1:-} == "--check" ]]; then
    # One invocation; clang-format skips what .clang-format-ignore names and
    # reports each unformatted line as an error.
    if ! "$format" --dry-run -Werror "${files[@]}"; then
        echo >&2
        echo "format.sh: the files above are not formatted. Run tools/format.sh" >&2
        exit 1
    fi
    if ! "${ruff[@]}" format --check --quiet "${pyfiles[@]}"; then
        echo >&2
        echo "format.sh: the python files above are not formatted. Run tools/format.sh" >&2
        exit 1
    fi
    if ! beautify check "${webfiles[@]}"; then
        echo >&2
        echo "format.sh: the web files above are not formatted. Run tools/format.sh" >&2
        exit 1
    fi
    echo "format.sh: ${#files[@]} files, ${#pyfiles[@]} python and ${#webfiles[@]} web files are formatted"
    exit 0
fi

"$format" -i "${files[@]}"
"${ruff[@]}" format --quiet "${pyfiles[@]}"
beautify replace "${webfiles[@]}"
echo "format.sh: formatted ${#files[@]} files, ${#pyfiles[@]} python and ${#webfiles[@]} web files"
