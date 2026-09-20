# Optional scalar Map keys and remainder indices — 2026-09-20 UTC

Continued clean `6c187552` and its recorded B/Data+B retained-receiver boundary.
`codex-wip-20260907` exists but is already an ancestor of `ctcompile-v1`; there
was no unmerged rescue. A service interruption left this session's drafts and
build incomplete. Replacement agents finished the same fixture/review tasks;
the root resumed the build and preserved the escape draft.

Linux checked 17 readable executable/CLI identities with no Claude match and
57 unreadable identities. Windows Get-CimInstance checked 344 processes with
no match. Availability remained uncertain, so concurrent restrictions applied.
No browser/runtime-oracle/shared-file edits or push.

## Landed

- `7b456d79`: optional Number/Boolean/Null/Undefined Map keys reuse the existing
  finite `nullable_scalar` carrier. Admission and emission retain the actual
  key tag; null, undefined, false and zero remain different keys. Both ordered
  and associative storage use SameValueZero for numeric keys, including NaN,
  and canonicalize numeric negative zero on insertion without changing payloads.
- Five unchanged source cases now execute natively: `class-map-direct`,
  `class-map-distinct`, `class-map-inherited`, `class-map-helper-numeric-key`
  and `class-map-nested-holder`. Six new cases cover absent and Boolean keys,
  NaN, signed zero, saved reads after overwrite/delete and inherited helpers.
  These eleven cases add **88 native executions**. Original files 01–23 remain
  byte-identical.
- Optional-key snapshots remain explicitly refused: the existing numeric vector
  carrier would discard their tags. Numeric value snapshots still work and add
  **eight native executions** with ordered storage. Mixed String/Number and
  object/absent class keys remain refused. The latter has a boxed schema and is
  checked separately from the existing optional-key diagnostics.
- `4a0e7910`: remainder indices accept a bounded nonnegative integer Number
  dividend and a positive invariant integer Number divisor. The conservative
  interval is `[0, min(lastDividend, divisor - 1)]`. Exact replay preserves
  unwritten children and saved aliases; the complete reload/later-store census,
  bounds, depth and work limits remain. Negative, fractional, non-Number, zero
  and varying divisors retain conservative refusals.
- `f428fd97`: three existing Map refusal RUN lines use `optimize=false` so
  precomputation does not remove their conditional branches. JavaScript bodies
  and diagnostic expectations are unchanged. This fixes the focused failures
  in `maps.mlir` and `map-flow.mlir` without changing compiler semantics.

Inspected generated class records, borrowed receiver pointers, direct helpers,
existing `shared_ptr<number_map<nullable_scalar>>` owners and numeric vectors.
There is no new class ownership graph or Script/VM/GC dependency. This does not
claim by-value Map emission or support for storing class receivers.

## Focused validation

All devbox actions held `/tmp/ctbrowser-devbox-build.lock`. Evidence and scripts:
`/tmp/ctcompile-scalar-map-keys-0949/`.

- Explicit remote targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-runtime`, `ctcompile-test-host-contract`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
- Exact `ctcompile_native_runtime`: **1/1**, 0.01s (0.02s total). Exercises both
  storage layouts, comparator/equality agreement and transitivity, numeric zero
  normalization, NaN deletion/reinsertion and saved scalar payloads.
- Exact `ctcompile_host_contract`: **1/1**, 0.48s (0.49s total).
- Exact `ctcompile_escape_analysis_arrays`: **1/1**, 1.47s (1.48s total).
- Escape lit `remainder-index-overwrite.test`, `bitand-index-overwrite.test`
  and `composed-index-overwrite.test`: **3/3**, 0.14s.
- New `CTNative/Lowering/Maps/nullable-scalar-keys.mlir`: **1/1**, 0.06s,
  covering numeric values and refused key snapshots in both optimization modes.
- Adjacent `CTNative/Lowering/Maps/{maps,map-flow}.mlir`: **2/2**, 0.29s after
  the three RUN flag corrections described above.
- Selected class probe: **27 observations / 128 main native executions / 54
  unprepared and 37 preparation refusals**, plus **four native boundary controls**.
  GCC/Clang, explicit/deduced C++ and both optimization settings are covered.
  Ancillary controls: **16 constructed-method executions / 20 refusals**,
  and **eight original-r executions / four refusals**.
- Ordered numeric value snapshots: **eight native executions**, with VM result
  `a=360`; Node also agrees. The copied snapshot survives later Map mutation.
- Remainder oracle: **63 claims / 63 observed sites / nine sound / nine of 15
  confined precision (60%)**. Zero violations, partial, pending, unobserved claims
  or unclaimed sites; six imprecise Stored claims remain.
- Nine native and four escape tested hashes match. Node validates 11 new native
  fixture observations and 21 remainder syntax/termination/retention cases.
- Completed required `tools/format.sh --check`: **20 existing diagnostics in six
  unchanged files**. Changed C++ formatting, Python AST/Black and diff whitespace
  checks pass; this is not a repository-wide formatting pass.

The first new pure fixture used free `undefined`, which this importer treats as
a host read. New fixtures use `void 0` to supply the primitive value. No older
source was changed. The first class run passed the six new positives before its
new object-key control exposed an overly narrow test assertion; that assertion
now pins the measured boxed-key refusal. The final class run passed afterward.

Skipped: full CTest/compiler lit, whole class-initialization lit, DOM replay,
broad native/corpus matrices, full Bootstrap, WPT/test262, Windows and sanitizers.
Historical browser compliance measurements were not changed or remeasured.

## Next boundary

Original B and Data+B still refuse
`class own-key snapshot constructor observes its receiver` at registration.
The numeric-key carrier work does not advance that ownership proof.

`ClassInitialization/Sources.cpp::borrowedHelperReads` permits forwarding and
own-field reads, while `ownFieldSnapshots` and `fieldsOnly` independently reject
retention. `ClosureLifting/Rewrite.cpp` creates the frame-owned record; the
constructor/shape checks and `TypeInference` receiver families must agree with
any Map alias extension. Existing scalar `ObjectIdentityType` storage cannot
stand in for a typed class record.

The next coherent ownership slice needs concrete borrowed record pointers with
a proved enclosing owner lifetime, complete constructor/helper/Map invocation
and payload census, and every alias use before that owner ends. Saved aliases
must remain valid after overwrite/delete; a failed constructor must not leave
a reachable partial receiver. Escaping Maps, returned aliases and cross-call
owner ambiguity need their own proof rather than a shared ownership graph.
Keep original nested Maps, the conflict branch, `e.set`, `e.remove`, `P.off`
and all configuration/disposal effects.

Captured lexical super-method targets, static captures, variable fields,
inherited getter/DOM targets, selectors/events/Popper, full Bootstrap and the
application driver remain unfinished.
