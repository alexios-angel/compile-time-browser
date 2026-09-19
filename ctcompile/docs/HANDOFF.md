# Handoff: continuing ctcompile

> NOTE (Claude, 2026-09-16): `ctcompile-v1` history was **reworded** while Codex
> was stopped - every unpushed commit from `f7966251` (origin) forward now has a
> `ctcompile(<area>): ...` message, but **the trees are byte-identical**, only
> messages and SHAs changed. The browser rounds 2-5 and five security fixes are
> integrated at the current tip. Pre-reword tips are kept as
> `ctcompile-v1-backup-premsg2` / `-premsg`. Full detail is in the
> `SESSION HANDOFF` journal at the end of `../../AGENT-SYNC.md`. Just branch from
> the current `ctcompile-v1` tip - nothing about the native work changed.

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Post-super calls and scaled overwrites, 2026-09-19 UTC

**7c4c4866** resumes B's post-super ordinary-call thread: four positive sources
add **32 native executions**, with receiver, method identity and ordering checks
retained. **eb8db016** proves bounded scaled array overwrites and preserves growth,
alias and cycle refusals. All prior source fixtures remain.

Focused host **1/1**, class initialization/DOM lit **2/2 (308.94s)**, arrays **1/1**,
and scaled/quotient/offset escape lit **3/3** passed. Class: **195 observations /
524 main native executions / 390 unprepared and 225 preparation refusals**.
DOM remains **632 observations / eight executions / 4,910 refusals**. Scaled
oracle: **54 sites / seven sound / 7 of 10 precision**, zero violations or
unclaimed/partial/pending sites. Nine source hashes match. Formatter retains
20 baseline diagnostics in six untouched files; changed checks pass. Full suites
and broad matrices were skipped.

**Next:** Qi's immutable lexical-home/base lookup, retaining the leaf receiver.
The preserved 118-result inherited-dispatch now reaches its receiver/prototype
refusal. Captured helpers, full Bootstrap behavior, broader ownership and the
application driver remain. The user is switching to the updated unattended loop,
which now renders live JSON events as readable activity and retains raw logs.

[Exact changes, commands, measurements and loop validation](handoff/2026-09-19-post-super.md).

## CMake ownership, 2026-09-19 UTC

**099852d2** atomically merges the CMake hygiene work. All **192 maintained C++
folders** now have local CMake ownership; 183 `CMakeLists.txt` files were added.
Sibling source lists and parent-relative CMake paths are gone. Existing compiled
sources, flags, link commands, executable paths and test settings are preserved,
with normalized paths and a relocated test PCH. The new deduction executable is
optional; the new `cmake_hygiene` CTest checks ownership and paths.

Ten distinct focused CTests and two lit cases passed. Browser-only configuration
with LLVM/MLIR disabled, the installed package consumer, and a standalone
MLIR-off compiler build passed. Four missing public files and installed subsystem
aliases were fixed. The pinned formatter retains 20 baseline diagnostics in six
untouched files; changed Python formatting passes. Full suites and corpus runs
were skipped. [Exact changes and validation](handoff/2026-09-19-cmake-hygiene.md).

The next native boundary remains B's `this._getConfig(t)` after its own `super()`,
then Qi's lexical home/base lookup. No native admission or runtime behavior changed.

## Repository file splits, 2026-09-19 UTC

**76df4a57** atomically merges the browser cleanup after separate area commits.
Compiler splits landed separately, ending with **7f003cff** for the native DOM
test drivers and preserved controls. All **92 maintained files** formerly over
1,000 lines are now split. Seven vendor/upstream files remain untouched.
Handwritten C++ uses real `.hpp` headers and separately compiled `.cpp` files.

Focused compiler CTests **6/6**, browser CTests **35/35**, class/escape lit **3/3**,
recorder, citation and the complete native DOM Strings lit passed. DOM Strings measured
**801 observations / eight binaries / 1,000 source refusals**. Three outdated
controls were corrected while preserving their source and relevant refusals;
no compiler admission or runtime behavior was changed. The pinned formatter
retains 20 baseline diagnostics in six untouched files; changed files pass.
AGENT-SYNC history is archived under the user's approval. Full suites and broad
matrices were skipped. [Detailed changes and validation](handoff/2026-09-19-file-splits.md).

All eight devbox source copies match home by checksum. Twelve retired source
copies remain cache-only; all 49 previously recorded build/tool roots survive.
The temporary browser worktree was properly removed.

**Next native boundary is unchanged:** B's ordinary `this._getConfig(t)` after
`super()`, then Qi's lexical home/base lookup. The full Bootstrap path and the
application driver remain unfinished. The merged class/DOM and citation checks
also passed.

## Nearest method overrides and exact quotient overwrites, 2026-09-19 UTC

Resumed clean **171eee2a** and the nearest-override thread claimed by the
session explicitly abandoned at 14:54:19 in AGENT-SYNC. Both agents' histories,
unmerged branches, standing protocol and handoffs were reviewed. September 7 WIP
is already an ancestor; no recovery merge was needed. Three agents supplied
native dispatch review, the authentic Bootstrap continuation and an independent
quotient-index escape draft. Initial service limits interrupted their first
turns; all resumed tasks completed. Root reviewed, gated and committed each area
separately. Independent final native review found no blocker.

**0414e5fb** selects the nearest ordinary method on a proved local inheritance
chain. Every ancestor body, including shadowed definitions, still passes the
complete receiver and source census. Unused base/method identities disappear only
after checking callable and symbol uses. Four new positive sources plus the
preserved `inherited-method-override` (21) add **40 native executions**. Distinct
leaf targets in a shared method still refuse. Nine new sources were added; all
185 previous split-file sections remain unchanged.

**5019e45f** proves bounded `i / divisor` overwrites for exact positive Number
divisors with divisible starts and strides. Each write range keeps its actual
quotient stride; recursive guard reloads must miss every write. Saved children,
remaining aliases, historical cycles and work limits remain. Added CFG/SCF controls
and 17 source functions.

Focused host **1/1 (0.48s; 0.49s total)**; class initialization/DOM lit **2/2
(313.53s)**: **187 source observations / 492 main native executions**, with
374 unprepared and 221 preparation refusals. DOM remains **632 observations /
eight executions / 4,910 refusals**. Arrays **1/1 (1.28s; 1.29s total)** and
quotient/offset/visited escape lit **3/3 (0.11s)** pass. New oracle: **51 sites /
six sound / 6 of 9 confined precision**, zero violations, partial, pending or
unclaimed sites. Seven tested hashes match; changed formatting passes. Required
formatter retains 26 baseline diagnostics in nine unchanged files.

**Next:** preserve B's ordinary `this._getConfig(t)` call after its own `super()`
initialization. The original 118-result `inherited-dispatch` now stops there with
`super initialization contains an unproved call`, before reaching Qi's method.
Then normalize Qi's `LoadHome -> GetProto -> GetProperty -> Call` using its immutable
method home and immediate base, while retaining Qi as receiver. Complete authentic
W/B/Qi still needs captured constructor helpers, ordinary static methods,
receiver-selected getters, complete H/r/s bodies, configuration, selectors,
events and Popper. Own-data provenance, broader escape control flow/ownership and
the application driver remain. Full suites/broad matrices/whole Bootstrap were
skipped; no browser/Script changes or new compliance measurements. Exact commands,
initial failures and resume provenance: HANDOFF and `/tmp/ctcompile-overrides-1456/`.

Focused devbox commands, all under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- The same CTest command with `-R '^ctcompile_escape_analysis_arrays$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(quotient-index-overwrite|offset-index-overwrite|visited-index-overwrite)[.]test$'`.

The corrected narrow native probe measured **32 observations / 112 executions /
64 unprepared and 30 preparation refusals**, plus 24 existing executions/24
refusals and two prepared refusals. The final initialization case additionally
measured 28 executions/30 refusals and 13 prepared refusals. Existing escape
oracles retain **48 sites / seven sound / 7 of 13 precision** and **31 / six /
6 of 7**, all with zero violations, partial, pending and unclaimed sites.

Initial native probes exposed the new fixtures missing the existing generic-IR
printing workaround for `cf.switch`, then unobserved shadowed closures blocking
admission. The first cleanup draft invalidated the module walk; the final code
collects candidates without mutation, erases all closures, then module-level
bodies. The final narrow probe and focused class/DOM gate passed afterwards.
Escape build/tests passed on the first run. Generated middle-override C++ was
inspected: stack object, borrowed receiver and direct selected-method calls;
no Script/VM or prototype storage. Evidence includes all failed/passing logs,
commands, hashes and `override-middle.cpp` in `/tmp/ctcompile-overrides-1456/`.

Required `tools/format.sh --check` stops in the C++ phase with the baseline
26 diagnostics in nine HEAD-identical files. Changed C++/Python formatting,
Python syntax, temporary shell-script syntax, source JavaScript checks and
`git diff --check` pass. Full CTest/compiler lit, standalone transaction/lifetime,
broad native/corpus matrices, WPT/test262 and whole Bootstrap were skipped.
Focused passes are not full-suite results. No browser/runtime edits or push.
Removed only this session's temporary SSH authorized entry and local keypair;
other keys were preserved.

For the next constructor slice, retain only proved same-receiver ordinary calls
after initialization phase 4, preserving argument/effect order. Let the subsequent
`fieldsOnly` proof record the cloned calls; recording original operations before
`takeBody` creates stale pointers. Keep pre-super/foreign/dynamic calls, method
replacement, new.target, declarations/global effects and budget failures refused.
The original source chain is B constructor -> Qi override -> lexical B method ->
ordinary W merge -> Qi constructor.Default. Standalone super-property reads use
`__ctbrowser_super_get`; the called-method IR above is a different shape.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc"></a>
- [Inherited method targets and bounded offset overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc)
<a id="explicit-super-construction-and-visited-index-reloads-2026-09-19-utc"></a>
- [Explicit super construction and visited-index reloads, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#explicit-super-construction-and-visited-index-reloads-2026-09-19-utc)
<a id="ordered-class-ancestry-and-disjoint-overwrite-reloads-2026-09-19-utc"></a>
- [Ordered class ancestry and disjoint overwrite reloads, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#ordered-class-ancestry-and-disjoint-overwrite-reloads-2026-09-19-utc)
<a id="inheritance-helper-declarations-and-invariant-overwrites-2026-09-19-utc"></a>
- [Inheritance helper declarations and invariant overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#inheritance-helper-declarations-and-invariant-overwrites-2026-09-19-utc)
<a id="aliased-overwrite-receivers-and-inheritance-boundary-2026-09-19-utc"></a>
- [Aliased overwrite receivers and inheritance boundary, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#aliased-overwrite-receivers-and-inheritance-boundary-2026-09-19-utc)
<a id="constructor-origin-dom-calls-2026-09-19-utc"></a>
- [Constructor-origin DOM calls, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#constructor-origin-dom-calls-2026-09-19-utc)
<a id="actual-h-callers-and-guarded-array-overwrites-2026-09-19-utc"></a>
- [Actual H callers and guarded array overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#actual-h-callers-and-guarded-array-overwrites-2026-09-19-utc)
<a id="confined-unused-local-cells-2026-09-19-utc"></a>
- [Confined unused local cells, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#confined-unused-local-cells-2026-09-19-utc)
<a id="unused-conditional-and-early-return-bodies-2026-09-19-utc"></a>
- [Unused conditional and early-return bodies, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#unused-conditional-and-early-return-bodies-2026-09-19-utc)
<a id="dynamic-bootstrap-f-and-saved-digit-keys-2026-09-19-utc"></a>
- [Dynamic Bootstrap F and saved digit keys, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#dynamic-bootstrap-f-and-saved-digit-keys-2026-09-19-utc)
<a id="known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc"></a>
- [Known matching F inputs and ASCII String indices, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc)
<a id="inert-unused-bodies-and-ascii-string-lengths-2026-09-19-utc"></a>
- [Inert unused bodies and ASCII String lengths, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#inert-unused-bodies-and-ascii-string-lengths-2026-09-19-utc)
<a id="full-h-instance-methods-and-object-reload-prerequisite-2026-09-19-utc"></a>
- [Full H instance methods and object-reload prerequisite, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#full-h-instance-methods-and-object-reload-prerequisite-2026-09-19-utc)
<a id="full-h-entry-calls-and-invariant-array-lengths-2026-09-19-utc"></a>
- [Full H entry calls and invariant array lengths, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#full-h-entry-calls-and-invariant-array-lengths-2026-09-19-utc)
<a id="original-h-key-normalization-and-invariant-array-reloads-2026-09-19-utc"></a>
- [Original H key normalization and invariant array reloads, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#original-h-key-normalization-and-invariant-array-reloads-2026-09-19-utc)
<a id="repeated-helpers-across-entry-exits-and-budgeted-invariant-depth-2026-09-19-utc"></a>
- [Repeated helpers across entry exits and budgeted invariant depth, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#repeated-helpers-across-entry-exits-and-budgeted-invariant-depth-2026-09-19-utc)
<a id="conditional-dataset-iterators-and-invariant-bitwise-latches-2026-09-19-utc"></a>
- [Conditional dataset iterators and invariant bitwise latches, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#conditional-dataset-iterators-and-invariant-bitwise-latches-2026-09-19-utc)
<a id="sequential-dataset-iterators-and-invariant-addsub-2026-09-19-utc"></a>
- [Sequential dataset iterators and invariant Add/Sub, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#sequential-dataset-iterators-and-invariant-addsub-2026-09-19-utc)
<a id="direct-dataset-helpers-and-nested-invariant-latches-2026-09-19-utc"></a>
- [Direct dataset helpers and nested invariant latches, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#direct-dataset-helpers-and-nested-invariant-latches-2026-09-19-utc)
<a id="class-dataset-loops-and-invariant-powers-2026-09-19-utc"></a>
- [Class dataset loops and invariant powers, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#class-dataset-loops-and-invariant-powers-2026-09-19-utc)
<a id="confined-class-callbacks-and-invariant-division-2026-09-19-utc"></a>
- [Confined class callbacks and invariant division, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#confined-class-callbacks-and-invariant-division-2026-09-19-utc)
<a id="shared-utf-16-indexing-and-invariant-products-2026-09-19-utc"></a>
- [Shared UTF-16 indexing and invariant products, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#shared-utf-16-indexing-and-invariant-products-2026-09-19-utc)
<a id="dataset-loops-beside-native-class-construction-2026-09-19-utc"></a>
- [Dataset loops beside native class construction, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#dataset-loops-beside-native-class-construction-2026-09-19-utc)
<a id="combined-helper-captures-and-literal-bitnot-latches-2026-09-18-utc"></a>
- [Combined helper captures and literal BitNot latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#combined-helper-captures-and-literal-bitnot-latches-2026-09-18-utc)
<a id="captured-h-objects-and-literal-unary-latches-2026-09-18-utc"></a>
- [Captured H objects and literal unary latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-h-objects-and-literal-unary-latches-2026-09-18-utc)
<a id="captured-local-h-helpers-and-boolean-add-latches-2026-09-18-utc"></a>
- [Captured local H helpers and Boolean Add latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-local-h-helpers-and-boolean-add-latches-2026-09-18-utc)
<a id="local-callable-holders-and-negative-string-latches-2026-09-18-utc"></a>
- [Local callable holders and negative String latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#local-callable-holders-and-negative-string-latches-2026-09-18-utc)
<a id="captured-dataset-filter-callbacks-and-negative-strings-2026-09-18-utc"></a>
- [Captured dataset-filter callbacks and negative Strings, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-dataset-filter-callbacks-and-negative-strings-2026-09-18-utc)
<a id="captured-bootstrap-f-replacement-proof-2026-09-18-utc"></a>
- [Captured Bootstrap F replacement proof, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-bootstrap-f-replacement-proof-2026-09-18-utc)
<a id="captured-sibling-bootstrap-m-2026-09-18-utc"></a>
- [Captured sibling Bootstrap M, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-sibling-bootstrap-m-2026-09-18-utc)
<a id="entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc"></a>
- [Entry-local Bootstrap M and primitive addition, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc)
<a id="mixed-classdom-intrinsics-and-primitive-subtraction-2026-09-18-utc"></a>
- [Mixed class/DOM intrinsics and primitive subtraction, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#mixed-classdom-intrinsics-and-primitive-subtraction-2026-09-18-utc)
<a id="declared-error-classdom-composition-and-primitive-powers-2026-09-18-utc"></a>
- [Declared Error class/DOM composition and primitive powers, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#declared-error-classdom-composition-and-primitive-powers-2026-09-18-utc)
<a id="config-defaults-and-primitive-bitwise-snapshots-2026-09-18-utc"></a>
- [Config defaults and primitive bitwise snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#config-defaults-and-primitive-bitwise-snapshots-2026-09-18-utc)
<a id="transitive-method-arguments-and-primitive-division-2026-09-18-utc"></a>
- [Transitive method arguments and primitive division, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#transitive-method-arguments-and-primitive-division-2026-09-18-utc)
<a id="original-class-method-arguments-and-primitive-products-2026-09-18-utc"></a>
- [Original class-method arguments and primitive products, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#original-class-method-arguments-and-primitive-products-2026-09-18-utc)
<a id="constructor-stored-dom-methods-and-primitive-unary-snapshots-2026-09-18-utc"></a>
- [Constructor-stored DOM methods and primitive unary snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#constructor-stored-dom-methods-and-primitive-unary-snapshots-2026-09-18-utc)
<a id="captured-local-class-getters-2026-09-18-utc"></a>
- [Captured local class getters, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#captured-local-class-getters-2026-09-18-utc)
<a id="original-classdom-composition-and-string-bitnot-2026-09-18-utc"></a>
- [Original class/DOM composition and String BitNot, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#original-classdom-composition-and-string-bitnot-2026-09-18-utc)
<a id="confined-dom-fields-and-canonical-string-powers-2026-09-18-utc"></a>
- [Confined DOM fields and canonical String powers, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#confined-dom-fields-and-canonical-string-powers-2026-09-18-utc)
<a id="direct-dom-receivers-and-canonical-string-bitwise-snapshots-2026-09-18-utc"></a>
- [Direct DOM receivers and canonical String bitwise snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#direct-dom-receivers-and-canonical-string-bitwise-snapshots-2026-09-18-utc)
<a id="explicit-class-entries-and-canonical-string-division-2026-09-18-utc"></a>
- [Explicit class entries and canonical String division, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#explicit-class-entries-and-canonical-string-division-2026-09-18-utc)
<a id="shared-dom-preparation-and-canonical-string-products-2026-09-18-utc"></a>
- [Shared DOM preparation and canonical String products, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#shared-dom-preparation-and-canonical-string-products-2026-09-18-utc)
<a id="native-global-helpers-and-canonical-string-unary-snapshots-2026-09-18-utc"></a>
- [Native global helpers and canonical String unary snapshots, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#native-global-helpers-and-canonical-string-unary-snapshots-2026-09-18-utc)
<a id="recovered-global-holders-and-string-left-subtraction-2026-09-18-utc"></a>
- [Recovered global holders and String-left subtraction, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-global-holders-and-string-left-subtraction-2026-09-18-utc)
<a id="local-callable-holders-and-signed-string-subtraction-2026-09-18-utc"></a>
- [Local callable holders and signed String subtraction, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#local-callable-holders-and-signed-string-subtraction-2026-09-18-utc)
<a id="original-class-method-r-and-signed-unary-literals-2026-09-18-utc"></a>
- [Original class-method r and signed unary literals, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#original-class-method-r-and-signed-unary-literals-2026-09-18-utc)
<a id="closed-scalar-guards-and-original-bootstrap-helpers-2026-09-18-utc"></a>
- [Closed scalar guards and original Bootstrap helpers, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#closed-scalar-guards-and-original-bootstrap-helpers-2026-09-18-utc)
<a id="recovered-declaration-borrows-and-bounded-powers-2026-09-18-utc"></a>
- [Recovered declaration borrows and bounded powers, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-declaration-borrows-and-bounded-powers-2026-09-18-utc)
<a id="local-helper-proofs-and-power-identities-2026-09-18-utc"></a>
- [Local helper proofs and power identities, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#local-helper-proofs-and-power-identities-2026-09-18-utc)
<a id="getter-cleanup-and-signed-right-shifts-2026-09-18-utc"></a>
- [Getter cleanup and signed right shifts, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#getter-cleanup-and-signed-right-shifts-2026-09-18-utc)
<a id="recovered-error-getters-and-signed-bitwise-snapshots-2026-09-18-utc"></a>
- [Recovered Error getters and signed bitwise snapshots, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-error-getters-and-signed-bitwise-snapshots-2026-09-18-utc)
<a id="recovered-literal-throws-and-signed-bitnot-2026-09-18-utc"></a>
- [Recovered literal throws and signed BitNot, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-literal-throws-and-signed-bitnot-2026-09-18-utc)
<a id="local-constructor-getter-reads-2026-09-18-utc"></a>
- [Local constructor getter reads, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#local-constructor-getter-reads-2026-09-18-utc)
<a id="recovered-method-counters-and-signed-subtraction-2026-09-18-utc"></a>
- [Recovered method counters and signed subtraction, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#recovered-method-counters-and-signed-subtraction-2026-09-18-utc)
<a id="recovered-method-dispatch-continuation-2026-09-18-utc"></a>
- [Recovered method dispatch continuation, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#recovered-method-dispatch-continuation-2026-09-18-utc)
<a id="structured-class-methods-and-signed-division-2026-09-18-utc"></a>
- [Structured class methods and signed division, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#structured-class-methods-and-signed-division-2026-09-18-utc)
<a id="fresh-config-defaults-and-signed-number-products-2026-09-17-utc"></a>
- [Fresh Config defaults and signed Number products, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#fresh-config-defaults-and-signed-number-products-2026-09-17-utc)
<a id="filtered-prefix-assignments-and-number-cancellation-2026-09-17-utc"></a>
- [Filtered prefix assignments and Number cancellation, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#filtered-prefix-assignments-and-number-cancellation-2026-09-17-utc)
<a id="ordered-native-result-assignments-and-negative-sub-snapshots-2026-09-17-utc"></a>
- [Ordered native result assignments and negative Sub snapshots, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#ordered-native-result-assignments-and-negative-sub-snapshots-2026-09-17-utc)
<a id="validated-element-guards-and-nested-helper-calls-2026-09-17-utc"></a>
- [Validated element guards and nested helper calls, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#validated-element-guards-and-nested-helper-calls-2026-09-17-utc)
<a id="present-dataset-values-and-signed-unary-snapshots-2026-09-17-utc"></a>
- [Present dataset values and signed unary snapshots, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#present-dataset-values-and-signed-unary-snapshots-2026-09-17-utc)
<a id="integrated-runtime-recovery-complete-2026-09-17-utc"></a>
- [Integrated-runtime recovery complete, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#integrated-runtime-recovery-complete-2026-09-17-utc)
<a id="original-snapshot-iteration-and-scalar-loop-completion-2026-09-16-utc"></a>
- [Original snapshot iteration and scalar loop completion, 2026-09-16 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#original-snapshot-iteration-and-scalar-loop-completion-2026-09-16-utc)
<a id="owning-snapshot-length-and-negative-number-strides-2026-09-16-utc"></a>
- [Owning snapshot length and negative Number strides, 2026-09-16 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#owning-snapshot-length-and-negative-number-strides-2026-09-16-utc)
<a id="original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc"></a>
- [Original dataset filter and moved-VM recovery, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc)
<a id="dataset-key-snapshots-and-dynamic-add-induction-2026-09-16-utc"></a>
- [Dataset key snapshots and dynamic Add induction, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#dataset-key-snapshots-and-dynamic-add-induction-2026-09-16-utc)
<a id="guarded-config-spreads-and-recovered-escape-work-2026-09-16-utc"></a>
- [Guarded Config spreads and recovered escape work, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#guarded-config-spreads-and-recovered-escape-work-2026-09-16-utc)
<a id="config-json-tags-and-reversed-array-guards-2026-09-16-utc"></a>
- [Config JSON tags and reversed array guards, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#config-json-tags-and-reversed-array-guards-2026-09-16-utc)
<a id="original-bootstrap-attribute-normalization-2026-09-16-utc"></a>
- [Original Bootstrap attribute normalization, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#original-bootstrap-attribute-normalization-2026-09-16-utc)
<a id="native-json-chain-recovered-and-gated-2026-09-16-utc"></a>
- [Native JSON chain recovered and gated, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#native-json-chain-recovered-and-gated-2026-09-16-utc)
<a id="helperuri-composition-the-json-chain-draft-and-the-audit-2026-09-15-utc"></a>
- [Helper/URI composition, the JSON chain draft and the audit, 2026-09-15 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#helperuri-composition-the-json-chain-draft-and-the-audit-2026-09-15-utc)
<a id="saved-nullable-uri-guards-and-fingerprinting-2026-09-15-utc"></a>
- [Saved nullable URI guards and fingerprinting, 2026-09-15 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#saved-nullable-uri-guards-and-fingerprinting-2026-09-15-utc)
<a id="earlier-measurements"></a>
- [Earlier measurements](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#earlier-measurements)
