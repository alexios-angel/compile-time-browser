# ctcompile 0.1.0 developer preview

This is the initial release scope and preparation checklist. It is not a claim
that a release tag or downloadable binaries have been published.

## Scope

The preview exposes two workflows documented in the [README](../README.md):

- **Application packaging:** `ctcompile` loads an application through ctbrowser,
  packages HTML, discovered resources and classic-script bytecode, and emits a
  `.ctapp` or appends that bundle to `ctrun`. The launcher uses the VM and checks
  that startup scripts load from their images.
- **Native compiler development:** `ctjs-translate` imports JavaScript and emits
  C++; `ctjs-opt` performs analysis and lowering. Proved programs use ordinary
  C++ values and ownership. Native browser calls use public ctbrowser subsystem
  APIs. The native output must not depend on `ctbrowser::script` or the boxed
  AOT ABI.
- **Installation:** the `ctcompile-tools` CMake component installs `ctcompile`,
  the MLIR drivers when enabled, and the matching launcher when built alongside
  ctbrowser. LLVM is a compiler dependency, not a dependency added to generated
  native programs.

The native application driver is incomplete. The packager does not invoke the
native pipeline, compile HTML into blueprints or precompile CSS. Full native
Bootstrap execution is not supported by this preview.

## Compatibility and limits

- The documented build requires C++23 and LLVM 23; 23.1.0 is the repository's
  tested LLVM/MLIR version. Other LLVM majors fail configuration. Native tools
  require `CTCOMPILE_ENABLE_MLIR=ON`.
- Treat compiler flags, dialects, generated C++ interfaces and serialized
  formats as preview interfaces. Keep the compiler and launcher from the same
  build; program images carry engine-format compatibility information.
- The tools component is a host installation, not a standalone SDK or a
  portable binary bundle. External library dependencies and fonts are not
  copied into it. Use the default static project libraries (`BUILD_SHARED_LIBS=OFF`).
  When ANGLE is enabled, retain its fetched directory in the checkout; the
  installed tools preserve that dependency path. Disable ANGLE at configure
  time for a build without that dependency or WebGL support.
  The native quickstart uses headers from the checkout.
- Packaging runs application scripts. Resource discovery covers the observed
  startup execution, with a 60-frame ceiling, rather than every possible user
  interaction. Missing resource and font warnings need attention. Module-script
  packaging is refused.
- Native admission depends on proved types, effects, identities and ownership.
  Arbitrary JavaScript, arbitrary host APIs and all exception/iterator shapes
  are not supported. A partially lowered MLIR module is not a native program.
- The current observing custom-iterator witnesses still stop at
  traversal and close-coverage proofs through retained invocation regions.
  Next/exhaustion state and unsuppressed close failures remain unfinished.
  [The measured handoff](handoff/2026-09-24-guarded-records-joined-length.md)
  records the exact boundary; it claims no new source admission for those cases.

## Release validation checklist

Preparation landed in `b2b8d184`, `566d4d96` and `280a8db9`. The CLI now
atomically replaces each output file, rejects output/manifest aliases and
entry/launcher overwrites, and reports bytecode packaging accurately in help.
Atomic replacement is per file: a later manifest error can leave the newly
written application in place.

Measured on 2026-09-24, Linux x86_64 devbox, Release configuration, the pinned
clang-std-embed compiler and LLVM/MLIR 23.1.0, CMake 3.28.3:

| Focused check | Result |
| --- | --- |
| `ctcompile_cli` | 1/1 passed, 0.41 s; aliases, literal `-` output and file-size-limited write failure preserve the expected files |
| `ctcompile_install`, MLIR enabled | 1/1 passed, 1.01 s; relocated tools, bundle/executable startup, module refusal, native `answer=42`, Script/AOT symbol checks and eval/syntax refusals |
| `ctcompile_install`, MLIR disabled | 1/1 passed, 0.26 s; packaging-only inventory and execution |
| Existing `ctcompile_exception_recovery` | 1/1 passed, 5.47 s; native implementation unchanged |
| `tools/format.sh --check` | 1126 C++, 159 Python and 114 web files passed |

The final CLI and MLIR-enabled install invocation passed 2/2 in 1.43 s.
The shared devbox configuration was restored to MLIR enabled afterward.
Builds used explicit targets: `ctcompile-tool`, `ctjs-opt`, `ctjs-translate`,
`ctbrowser-tool-ctrun`, `ctcompile-tool-ctbaseline`, `ctcompile-tool-ctpageload`
and `ctcompile-test-exception-recovery`, as needed for each check. Every devbox
build and check held `/tmp/ctbrowser-devbox-build.lock`.

An initial install assertion mistook fetched ANGLE libraries for compiled
build output. The final check permits exactly that documented external
dependency location and still rejects references to the compiled build
directory. A new relative/absolute output-alias regression first reproduced
the remaining overwrite, then passed after absolute-base normalization.
These failed attempts are not included in the pass counts above.

Skipped: full CTest/compiler lit, unrelated bundle/image unit tests, broad
corpus/matrix replays, full Bootstrap, WPT/test262, Windows/macOS, sanitizers,
fresh standalone-engine qualification and performance measurements. The
original Bootstrap iterator sources still refuse; no new native admission or
conformance gain is claimed. No tag, push or binary publication was performed.
Logs and hashes are retained in `../test-results/2026-09-24-initial-release`
relative to the monorepo root.

Run checks relevant to the release changes on the devbox, following
[CLAUDE.md](../../CLAUDE.md) and the [test instructions](../test/README.md).
Full CTest, full compiler lit, broad corpus/matrix runs, WPT and test262 require
an explicit request; a focused pass is not a full-suite pass.

- Build the packager, matching launcher and enabled native tools.
- Install `ctcompile-tools` into an empty prefix; check the installed tool
  inventory and `ctcompile --version`/`--help`.
- Package the small fixture with installed tools, parse its manifest and run
  the resulting executable away from its source directory. Check bundle-only
  operation and refusals for unsupported module scripts.
- Run the README's native example through the installed tools, compile its C++
  and check `answer=42`. Inspect native output and binary symbols for forbidden
  Script/boxed-AOT dependencies.
- Run the affected existing CLI, bundle/image and selected packaging/native
  regressions, then `tools/format.sh --check`.
- Record exact commands, results, environment and skipped coverage in the
  release handoff. Review the README against those results before publication.

No whole-plan completion, full native Bootstrap support, cross-platform release
qualification or new performance claim is implied by these focused checks.
