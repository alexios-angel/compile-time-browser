# Preparing ctcompile-v1 for main, 2026-09-24

**The parser dependency must be published before this branch can be merged
for other users.** The local build works, but a fresh checkout cannot fetch
the pinned JavaScript parser commit. No push, tag or merge into main was made.

## Comparison

The review started at `3ee1f76a47fd81b628926e119a8e64dfeaa9e343`, using
`git diff 33c93892794eb83db20b7f09b2a8b60ee59c40fc...3ee1f76a47fd81b628926e119a8e64dfeaa9e343`.
That is 3,522 commits and 2,960 changed files: browser development, the monorepo
move, compiler implementation and preview packaging. Both local main
(`33c93892`) and GitHub's main (`d495cd76`, checked with `git ls-remote`) are
ancestors. No conflict resolution or rebase is needed at those refs.

Parallel reviews covered standards, preview scope and dependency availability.
This is a bounded review of integration and release paths, not an exhaustive
correctness review of every commit. The standards and dependency agents hit
rate limits; the parent completed their concrete findings and validation.

## Standards

One finding: `CTNativeAnalysis` depended on build-only `ctbrowser-dom` and
`ctbrowser-style` target names. This violated the compiler's documented
installed-engine boundary. A separate MLIR-enabled configure reproduced CMake
generation errors for both dependencies. `05014744` uses the existing exported
`ctbrowser::dom` and `ctbrowser::style` targets. Standalone instructions also
disable the regression suite, which requires monorepo test support and tools.

## Spec

One finding, fixed in `de496dd3`: root onboarding selected an ignored compiler
binary and misstated CMake and LLVM requirements. It now documents direct
configuration with a system C++23 compiler, CMake 3.23+, LLVM 23, optional
matching MLIR, and CMake 3.25+ for presets. Stale fixed test counts were removed.

The [0.1.0 preview scope](release-0.1.0.md) is unchanged: a bytecode application
packager and separate native subset tools. Full native Bootstrap, the native
application driver, HTML blueprints and precompiled CSS remain unfinished;
they are not promised by this preview. Sampled native emission uses public
subsystem APIs and retains compile-time refusals for unproved programs.

Review totals: one standards finding and one spec finding, both corrected.
The dependency publication blocker below remains open.

## Dependency blocker

The gitlink `third-party/compile-time-javascript` pins
`83275ba11898de490705aedcb39b4a0eed887805`. Its configured upstream,
`alexios-angel/compile-time-javascript`, has main at
`41e23cdc3508f59ea7c0cddeb7b2ab2ac067d787`: the pin is 21 commits ahead.
Authenticated GitHub lookup returns HTTP 422, and fetching the exact pin into
an empty repository fails with `upload-pack: not our ref`. Those parser changes
are used by this branch; downgrading the pin would change the tested code.

The CSS pin `164c390486e0f4dccf21c717a07a906e1716b819` and nested containers pin
`e122a6a4a3a81a61709378d7eaef6c2aa96037d2` both resolve upstream. All three local
submodule checkouts are clean. A verified, complete-history
`ctjs-83275ba.bundle` preserves the missing parser history in the evidence
directory below; it is a recovery artifact, not a substitute for publication.

Before merging, publish the exact parser commit to an advertised ref in its
configured upstream, then verify recursive submodule initialization from a
fresh clone. Recheck main and the candidate tip because either may have moved.
The standing no-push instruction leaves publication to a separately authorized
step; no dependency or main history was rewritten here.

## Validation

The Linux x86_64 devbox uses CMake 3.28.3, the pinned clang-std-embed compiler
and LLVM/MLIR 23.1.0. Every build/test command holds the devbox lock.

- `tools/remote-build.sh all`: passed, 523 build steps; this runs no CTests.
- After the target fix, `tools/remote-build.sh ctcompile-tool ctbrowser-tool-ctrun
  ctjs-opt ctjs-translate`: passed.
- A separate MLIR-enabled build against the installed engine passed all 221
  build steps for `ctcompile-tool`, `ctjs-opt` and `ctjs-translate`; all three
  `--version` commands passed. Configuration used `-DBUILD_TESTING=OFF`, the
  engine's installed `ctbrowser_DIR` and LLVM/MLIR 23.1.0 package directories.
- `ctest --test-dir build --output-on-failure --no-tests=error
  -R '^(ctcompile_cli|ctcompile_install|ctcompile_exception_recovery)$'`:
  **3/3 passed, 7.12 s**. CLI: 0.46 s; exception recovery: 5.55 s; relocated
  installation: 1.10 s. The install check compiles and runs native `answer=42`,
  rejects forbidden Script/AOT binary symbols, and checks eval/syntax refusals.
- `tools/format.sh --check`: 1126 C++, 159 Python and 114 web files passed.
- README examples: seven Bash blocks pass `bash -n`; 29 local links resolve.
- Changed-file `git diff --check` passes. The complete historical branch diff
  still reports whitespace in copied LLVM fixtures and existing blank lines;
  those unrelated files were preserved.

The final CMake source hash matches the devbox. The shared monorepo build
remains MLIR enabled; the standalone experiment used separate build and
installation directories.

Full CTest/compiler lit, broad corpus/matrices, WPT/test262, sanitizers,
Windows/macOS and performance runs were not requested and were skipped.
No broad-suite pass or new native source admission is claimed.

Logs, pinned refs, the original spec review, parser recovery bundle and hashes
are retained in `../test-results/2026-09-24-main-merge` relative to the monorepo
root. [HANDOFF.md](HANDOFF.md) records the next native implementation boundary.
