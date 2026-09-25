# Constructor receiver borrows and signed output bands — 2026-09-20 UTC

Continued clean `546106d7` and its recorded registration receiver boundary.
The September 7 WIP was absent; no dirty interrupted draft remained. Independent
agents investigated registration, wrote native fixtures and drafted the escape
increment. Two agents hit a service rate limit; root preserved their work,
completed the native implementation and validated both changes.

Linux executable/CLI checks found no Claude identity but 57 nonempty processes
had unread executable identities. Windows Get-CimInstance checked 346 processes
with no Claude match. Availability remained uncertain, so concurrent restrictions
applied. No browser/runtime/shared-file changes or push.

## Landed

- `622f1ca8`: exact helper arguments may borrow the constructor receiver for
  ordinary named-field reads. The construction census checks fields present at
  the actual call; storing, returning, capturing, forwarding or snapshotting the
  borrowed receiver, and reading its methods/constructor, remain refused.
  Complete original helper bodies stay
  in the source census. The downstream argument fixpoint admits constructor
  receiver origins only after complete constructor setup checks, and final
  constructor admission still proves receiver uses and methods.
- Class preparation removes inert helper closures. Their private, capture-free
  functions now participate in the same borrowed-parameter fixpoint after all
  closure creations, implicit arguments, symbol references and direct callers
  are checked. An extra caller with an undefined argument removes the slot;
  public visibility or a module-level symbol reference withholds the proof.
- `04a622d3`: scaled signed-i32 endpoints may occupy one signed output band.
  Exact int64 multiplication and floor division identify the band before the
  existing bitwise evaluator computes endpoints. Input/output discontinuities,
  unbounded own indices and overlapping reloads remain refused. CFG and SCF
  checks cover both wrap directions, wide products, reversal, preserved gaps,
  retained children and hidden middle wraps. All 40 original oracle source
  bodies are unchanged; twelve sources were appended.

Six native positives add **48 executions**. Nineteen new sources cover direct,
captured and holder helpers, two receiver formals, inherited construction,
argument order and refusal boundaries. Original class inputs 01–18 and authentic
Bootstrap bodies are unchanged. The primitive-call source using global
`undefined` remains a preparation refusal; a separate `void 0` companion reaches
the intended native mixed-caller refusal. The missing-argument source preserves
its Node/interpreter exception.

Inspected captured/inherited C++ uses stack class records, borrowed pointers,
direct functions and field assignments. There is no heap owner, Script/VM symbol,
collector, closure environment or receiver table in either sample.

## Focused validation

Every devbox command held `/tmp/ctbrowser-devbox-build.lock`. Commands, logs,
hashes and generated samples are in `/tmp/ctcompile-borrowed-receivers-0747/`.

- Built explicit targets `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle` through `tools/remote-build.sh`.
- Exact `ctcompile_host_contract`: **1/1**, 0.49s (0.50s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.43s (1.44s total).
- Lit filter `^ctcompile :: CTNative/Lowering/Objects/(object-argument-lift|object-argument-refusals|constructor-refusals)[.]mlir$`:
  **3/3**, 4.17s.
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(right-shift|composed|left-shift)-index-overwrite[.]test$`:
  **3/3**, 0.14s. Existing left-shift recording/claims report **156 sites / 30
  sound / 30 of 35 confined precision (85.7%)**, zero violations, partial,
  pending or unclaimed sites. No corpus replay was used to read these metrics.
- Final selected class probe: all 19 borrow sources, 19 construction-method
  sources, 18 `OWN_FIELDS` cases, five helper/empty controls and original B/Data+B.
  **63 observations / 168 main native executions / 126 unprepared and 76
  preparation refusals**, plus **11 native boundary controls**. GCC/Clang,
  explicit/deduced C++ and both optimization settings are covered. New exact
  complete-proof budgets: global helper **2,060**, captured helper **1,606**,
  holder helper **1,669**, inherited helper **3,725**. Prior budgets remain
  **1,622 / 1,864 / 5,447 / 466 / 520 / 218 / 763 / 1,333**. Missing identities,
  forged annotations, duplicate source closures, partial budgets, original super
  roots, public helper visibility, module references and mixed callers pass.
  Ancillary controls: **16 constructed-method executions / 20 refusals** and
  **eight original-r executions / four refusals**.
- All seven native and four escape final tested source hashes match. Node
  checks cover all **19** new native sources (18 numeric, one exception) and
  all **52** escape functions for syntax, termination and retained child checks.
  Source preservation, changed C++ formatting, Python AST/Black and whitespace
  checks pass. An independent source review found no remaining lifetime blocker;
  its module-symbol census concern was fixed and has a runnable regression.
- Required `tools/format.sh --check`: **20 existing diagnostics in six
  HEAD-identical files**. Changed files pass. No clean repository-wide formatting
  result is claimed.

The first native probe passed the global helper and exposed the missing private
symbol connection on the captured case. The next six-positive probe passed.
Later probes exposed the synthetic global-undefined boundary and a stale Data+B
diagnostic; original sources were preserved and the final selection passed after
adding the literal companion and updating the expectation.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.

## Next boundary

Unchanged B refuses `class own-key snapshot constructor observes its receiver`.
Unchanged Data+B refuses `class method capture is not its constructor or an inert
sibling helper`: the complete Data holder captures the shared Map. The new borrow
proof resolves the registration helper but cannot grant retained-object ownership.

Use the existing captured Map and native ownership seams. Current
`HostContract/Analysis.cpp` accepts entry-local caller leaves with scalar fields;
`Analysis/OwnedGlobalMethods.cpp` requires entry-local family calls.
`Lowering/LoweringSupport.cpp::mapValueSpelling` has scalar, identity-object and
nested-Map carriers, without a typed class-record payload. Authentic registration
calls originate in constructors/dispose and store a receiver with object-valued
config and methods. That needs declared Map identity, a complete invocation and
alias proof, and a native owner/lifetime for every stored receiver. Preserve
saved results, overwrites/deletes, constructor failure, `e.remove`, `P.off` and
all original configuration/disposal effects. A later remove call alone proves
neither safe borrowing nor ownership.

Variable own-field presence and inherited per-leaf getter/dependency targets
remain separate proofs. Method-receiver forwarding, broader receiver borrows,
inherited DOM, static construction, selectors/events/Popper, full Bootstrap and
the application driver remain unfinished.
