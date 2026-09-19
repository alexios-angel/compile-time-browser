# File splits and preserved behavior, 2026-09-19

The user redirected this session to repository cleanup and splitting files over
1,000 lines. Resumed from `f8e37e5a`; native capability work itself is unchanged.

## What changed

The original inventory contained 99 tracked text files over 1,000 lines. All 92
maintained files are now split into smaller files. The seven remaining files are
five vendor bundles, the documented upstream `TranslateToCpp.cpp` fork, and the
ctjs regex port. These were left untouched as requested.

Handwritten C++ uses complete `.hpp` declarations/definitions and independently
compiled `.cpp` files. Initial statement-fragment drafts were replaced before
landing. Generated Unicode/entity data keeps `.inc` rows; AOT macro tables keep
`.def`. The two pre-existing Emit.inc fragments under Target/Cpp belong to the
vendored emitter and remain unchanged.

Compiler analysis, host contracts, recovery/ownership fixtures and native Python
drivers were split by concern. Browser DOM, layout, Style, Script and Shell splits
preserve original bodies and registration order. Public entry headers still work,
and all 32 newly included public files have install-list coverage. An independent
capture audit checked 200 original control callback capture lists and all 206
resulting lambdas. No browser behavior or native admission rule was changed.

Twelve long documentation files now have linked topic files. The final audit
checked 49 document files, 856 links and 746 original heading anchors; repeated
headings and headings containing raw HTML have explicit correct destinations.
Historical WPT/test262 measurements remain historical.

The WPT expectation storage contains the same 77,502 records in 162 parts; its
reader and writer support the partitioned default and flat custom files. All
179 p5 and 114 Phaser probes retain their bodies and order. Unicode data was
partitioned without re-downloading or regenerating its values; generators now
write bounded parts as well.

AGENT-SYNC.jsonl history was archived with user approval. The complete 14,024-line
original remains at `../AGENT-SYNC-archive/2026-09-19.md`; a final dated archive
also preserves the entries added during this session. The active file keeps the
protocol, live claims and latest 100 journal entries.

## Focused validation

All builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`. No local
C++ build ran.

- Compiler CTests: `ctcompile_type_inference`, `ctcompile_escape_analysis_arrays`,
  `ctcompile_owned_global_shared_map`, `ctcompile_host_contract`, and
  `ctcompile_exception_recovery`: 5/5, 251.60 seconds total.
- Compiler lit: `CTNative/Lowering/Objects/class-initialization.mlir`,
  `CTNative/Lowering/Objects/class-dom.mlir`, and
  `Analysis/Escape/escape-claims/fixture.test`: 3/3, 307.54 seconds.
- Native DOM Strings lit: `CTNative/Browser/native-dom-strings.test` passed in
  150.10 seconds: 801 Node/VM observations, eight GCC/Clang binaries, 1,000
  source refusals, 44 provenance/depth checks, 24 method checks, 241 capture
  checks, 101 replacement checks, 22 branch checks and 31 completion checks.
  Its replacement sub-gate also passed 11 observations and four executions.
- Recorder lit: `CTNative/Ownership/global-maps-recorder.test` passed: 12 native
  programs, 30 refusals, 434 observations and 13 distinguishing mutations.
- Browser CTests: 35/35, 1.20 seconds total, after explicit affected-target builds.
  Selected names: `html_parse`, `html5lib_fixtures`, `style_easing`, `css_syntax`,
  `css_values`, `css_values_color`, `css_shorthands`, `style_cascade`,
  `style_selectors`, `style_custom_properties`, `style_custom_functions`,
  `style_calc`, `layout_inline`, `layout_blocks`, `tables_and_markers`,
  `vm_functions`, `vm_stdlib`, `vm_async`, `vm_control_flow`, `promise_spec`,
  `regexp_model`, `forms_wpt`, `ranges_wpt`, `events_wpt`, `cssom_view`,
  `cssom_rules`, `cssom_sheets`, `cssom_declarations`, `cssom_nesting`,
  `custom_elements`, `web_animations`, `url_wpt`, `p5_api`, `phaser_api`,
  and `api_surface`.
- Generator storage test 1/1; WPT expectation storage tests 2/2. Python AST,
  export/factory snapshots, source reconstruction, syntax, CLI, changed-file
  formatting and whitespace checks passed. AOT and frame citation checks passed
  with 112 AOT file:line citations; frame records now cite stable handler names.

The required pinned formatter retains 20 baseline diagnostics in six untouched
files; all changed files pass it. The fallback Homebrew formatter completed the
browser checkout's full C++/Python/web check before the pinned version's three
formatting differences were reconciled. These are different formatter versions,
not an unqualified final whole-tree format pass.

Initial gates caught missing declarations/includes and cross-file helper visibility,
seven parameterless lambda captures, and a definition still naming a moved private
type alias. These were fixed before the passing browser run. Compiler fixture
CMake paths, DOM visitor references and an extracted test declaration were also
fixed before the passing compiler gates.

The native DOM gate contained three outdated controls. Original nested attribute
and nullable URI helper branches now have positive coverage without changing their
source. The old i32 -1 selector mutation is a valid unsigned-cast/default path;
four strict native-body comparisons preserve that control, while an actual
negative index still refuses. These capabilities predate this split; no compiler
or runtime rule was changed to make the tests pass.

Full CTest/compiler lit, broad native/corpus matrices, full WPT/test262,
sanitizers, and whole Bootstrap were not run. Unicode regeneration and the full
installed-package build were not run. Existing package omissions of
`script/dispatch.hpp` and `script/bytecode_opcodes.def` were observed and left
unchanged; all newly introduced public files are registered.

The merged tree also passed `class-dom.mlir` again: 632 observations, eight
combined native executions and 4,910 refusals (the two-case integration run took
286.78 seconds). That run caught a stale `vm.hpp:1519` frame-table reference.
The records now name `vm/frames_inline.hpp` and `run_loop/await.cpp`; no hook
flag changed. After that repair, `ctcompile_type_oracle` passed (0.01 seconds)
and `Core/def-citations.test` passed (0.06 seconds). There are six distinct
passing compiler CTests and six distinct passing compiler lit cases across
this session's focused runs. These are not full-suite results.

`95ad275f` makes source sync preserve `tools/llvm-mingw/` and
`examples-windows/`, alongside the existing build/tool exclusions. The first
merged sync spent several minutes copying these cached Windows assets; it
finished normally. A temporary local rsync check verified source creation and
deletion while preserving both changed and remote-only cached artifacts.
`bash -n tools/remote-build.sh` passed. The temporary browser worktree was
removed through `git worktree remove --force` after verifying its clean state
and merged ancestry; the seven retained worktrees were left alone.

Final checksum comparisons verified all eight devbox source copies against
home. Twelve retired source directories contain only protected caches, and all
49 recorded build/tool roots retain their original inodes and symlink targets.
The Windows toolchain and packaged outputs are present. The retirement check
ignores only parent-directory metadata and expected messages about preserving
nonempty `tools` and `third-party` directories. Evidence:
`final-mirror.log`, `final-mirror-tail.log` and `final-mirror-verification.json`
under the evidence directory below.

## Exact next native boundary

Resume B's ordinary `this._getConfig(t)` call after its own `super()` initialization.
The authentic inherited-dispatch probe still stops there with
`super initialization contains an unproved call`. Then handle Qi's lexical
`LoadHome -> GetProto -> GetProperty -> Call`, preserving its immutable method
home, immediate base and Qi receiver. Captured constructor helpers, ordinary
static methods, receiver-selected getters, complete H/r/s, configuration,
selectors, events, Popper, own-data provenance, broader escape ownership/control
flow and the application driver remain. No whole-Bootstrap success is claimed.

Detailed commands, failures, reconstruction audits and final inventories are in
`/tmp/ctcompile-file-splits-20260919/` and the task-specific audit paths recorded
in AGENT-SYNC. No push or unrelated worktree edits.
