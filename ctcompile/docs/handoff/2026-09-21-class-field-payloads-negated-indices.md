# Class field payloads and negated indices, 2026-09-21 UTC

Continued clean `b3c7edae` from HANDOFF and the current plan journal. Both commit
histories and unmerged branches were read; `codex-wip-20260907` is already an
ancestor. There was no unfinished code at entry. Linux cmdline/comm inspection
completed for 77 processes and Windows CIM for 348, without a Claude executable,
Node CLI or loop match. No browser or shared implementation was edited.

## Changes

`356373ed` reuses `retainedMapAliases` to retain each saved Map lookup's exact
original `ConstructOp`. DOM family proof now follows the complete class
examination and local Map use proof. An additional bounded source walk accepts
only primitive constructor stores and literal primitive returns, with confined
entry field/Map uses. It records field categories from explicit entry writes,
keyed by the original allocation and field name. A saved field read keeps its
read-time category through later mutation; a write to a different instance
cannot initialize it.

These facts exist only in the private class-preparation analyzer. They are
categories, never replacement values, source branch predicates or serialized
attributes. Constructor and Map operations remain in the prepared output.
Another record's field read cannot contribute without its own complete use
proof. The final source census and initial intrinsic-binding proof still run;
native source/owner analysis receives none of these provisional categories.

Five new witnesses publish `savedFirst.n`, `second.n`, their sum, or a saved
scalar read, while separately observing the unchanged full vendor class
computation. Holder and constructor variants retain five constructions and
eight functions. Controls cover absent entry writes, wrong-instance writes,
early reads, missing fields/records, object-valued writes, an unproved field
source, aliases escaping by call/capture, accessors, constructor effects and
replacement-object returns, plus manifest and budget failures.
The full original composite-result witnesses remain unchanged and refused.

`81e2c057` admits one outer minus on one already-proved binary operation with
direct Number literals. The same complete use census checks both operations;
further nesting, dynamic/coercing operands and escaped arithmetic remain refused.
Existing native negation, JavaScript Number arithmetic and UTF-16 clamping are
reused. Computed zero/one still provides no direct-literal casing authority.
Six exact former refusal source bodies now execute unchanged. Unicode, NaN,
infinities, signed zero, fractional/large bounds and captured descriptions are
covered.

## Focused validation

Explicit devbox targets, under the shared build lock:
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract` and
`ctcompile-test-native-reference`.

- Exact CTest `ctcompile_host_contract`: 1/1, 0.56 s, 0.57 s total.
- Selected lit `CTNative/Browser/native-class-dom-data.test`: PASS, 103.67 s;
  `CTNative/HostContract/dom-data-inputs.test`: PASS, 1.13 s.
  Parallel total: 103.68 s.
- New class fields: five Node/VM observations, five preparations, 29 refusals.
  Existing public family: five observations, three preparations, 29 refusals.
  Existing local DOM preparation: four observations/preparations, 30 preparation
  and eight incomplete-session refusals, 24 native object-key executions.
  Existing published records: nine observations, 56 native executions, 54 refusals.
- Negated-index selector: 80 native executions, 70 related proof refusals,
  eight Node/VM agreements and one known UTF-16/byte-index difference.
  GCC/Clang, both printing modes and both optimization policies ran, with
  native output and symbol gates.
- All ten final code/test hashes match the devbox.
- Seven C++ files pass the pinned scoped formatter; both Python files pass
  repository Black and syntax checks. All 113 earlier complete intrinsic cases,
  33 general refusals and five prior DOM source/fixture functions are preserved.
  The agent additionally checked 146 Node-only observations and 23 refusal bodies.
- Required repository `tools/format.sh --check` retains 16 pre-existing
  diagnostics in untouched `ctdrive.cpp`, `HostContract/ProviderPaths.h`,
  `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.

The first changed build found an explicit DenseMap initialization error; it was
fixed before passing builds. The first new selector admitted holder/constructor
fields and exposed an overly strict nested-read restriction on the distinct
instance. Read-only nested observers now remain allowed; nested writes remain
refused. Review found a missing RHS dependency census and an uncharged alias
walk; both were fixed and the final review was clear.

An earlier build heredoc consumed the following test commands; it counts only as
a build. The final gate redirects the build's stdin and explicitly ran both
tests. The String agent supplied its complete saved draft and selector despite
service interruptions; root handled builds, reconciliation and commits.

No full CTest/compiler lit, full intrinsic-export replay, broad corpus/matrix,
full Bootstrap, WPT/test262, Windows, new sanitizers, local native build or push
ran. Existing String executable mutation tests were skipped by the selector.

Evidence: `/tmp/ctcompile-class-result-build{0,1,2}.log`,
`/tmp/ctcompile-class-result-focused.log`,
`/tmp/ctcompile-class-result-gate{3,4}.log`,
`/tmp/ctcompile-class-result-final.json`,
`/tmp/ctcompile-class-result-format-final.log`,
`/tmp/ctcompile-string-next-focused.py` and
`/tmp/ctcompile-string-next-focused.log`.

## Next boundary

Preparation now carries an actual class field scalar into the published family,
but native class-session ownership still refuses. Compose the complete retained
constructor/local-Map proof with `environmentProblem` and
`OwnedGlobalRoots::analyzeMethodTable`, preserving exact source function,
allocation, call and field censuses. The host lowering path still needs an
explicit proved constructor lift; do not whitelist constructs or delete their
owners to bypass it.

Constructor-only field initialization and the full vendor composite `result`
need further scalar evidence. The latter also includes local Map observations
and Boolean-to-Number arithmetic, beyond the current Number-only category walk.
`published_class_fields()` supplies the admitted field preparation cases;
unchanged `published_source()` retains the complete-result refusals.

Multiple DOM-input alias partitions in local Maps, original B/Data+B, broader
String methods/indices, conditional callees, mutable cells, String ordering,
document views and the application driver remain. No full-Bootstrap admission
or new corpus coverage measurement is claimed.

