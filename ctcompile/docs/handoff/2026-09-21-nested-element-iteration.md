# Nested DOM and Bootstrap iteration, 2026-09-21

Continued clean `ac5c984a` and the loop-nested iterator boundary recorded in
`2026-09-21-element-for-of.md`. No uncommitted predecessor work or unmerged
`codex-wip-20260907` remained. Two agents independently handled the native fixture
and proof review/raw IR checks; root integrated the normalization and ran devbox
validation. The initial journal records the resumed thread and availability check.

`44b2e8f6` lets nested `for…of` traverse direct element query snapshots and unchanged,
vendor-pinned Bootstrap `R.find` results. An inner query runs in its original
outer body, sees preceding DOM writes, and retains the outer element's owner
and live Style. Guarded omitted outer receivers and explicit inner receivers use
the same public DOM/Style implementation as other native element calls.

The normalizer privately selects each importer's exact materialization arm, then
proves the complete live entry instead of inventing a trimmed loop prefix.
Original backedges, scalar state, document guards and effects remain present.
Every surviving direct input must have complete StringVector/ElementVector call
evidence; copied inputs pass the existing confined concat proof. The latter
returns an operation mapping, so subsequent member and length checks refer to
the live clone. A charged census rejects stale fingerprints and exhausted work.

Each materialized index needs its own copy-length guard before aliasing to the
original snapshot. Direct NodeList iteration then caps only its materialized
length at `min(size, 16777216)`; unrelated original aliases remain uncapped.
Confined concat keeps its existing cap. A separate spread of the same NodeList
is not mistaken for the for-of materialization. After capping, complete entry
proof runs again. Recorded producers, lengths and members may not be erased by
a later constant fold; such a candidate refuses before its operation identities
can become stale.

The original Bootstrap helper adds two requirements. Immutable callable-holder
reads may cross structured While boundaries, while slot writes remain unique,
unconditional and preceding the read. Exact bounded members cannot equal
`undefined`, so their default-receiver checks fold privately; each retained
member must then receive complete mapped index evidence. Nullable selector
results, mutable or conditionally initialized holders and escapes gain no fact.

Generated C++ continues to use indexed loops over `std::vector<js_element_t>`,
checked member access and ordinary public ctbrowser DOM/Style calls. Members
borrow the caller/session document; vectors own membership. No runtime type,
collector, VM iterator, Script symbol or browser/shared implementation change
was added. Literal C++ range-for printing remains unfinished.

## Focused validation

- Explicit devbox builds used `tools/remote-build.sh ctjs-opt ctjs-translate
  ctcompile-test-host-contract` under `/tmp/ctbrowser-devbox-build.lock`.
  Exact `ctcompile_host_contract` passed **1/1, 0.62 s; 0.63 s total** after the
  final member-proof change, using `ctest -R '^ctcompile_host_contract$'
  --output-on-failure --no-tests=error` in the remote build directory.
- Independent raw IR checks cover importer-shaped materialization, one cap,
  separate uncapped NodeList length, complete/incomplete evidence, stale and
  fresh fingerprints, forged reports, wrong original-length bounds, constant
  indices, escaped/unrelated materialization and discarded recorded inputs.
- Final selected lit filter
  `^ctcompile :: CTNative/Browser/native-dom-(nested-iteration|dataset).test$`
  passed **2/2, 136.90 s**, excluding 403 other discovered tests. The new nested
  case completed **48 native executions, two previous-source lowering checks
  and 94 refusals**. GCC/Clang, explicit/deduced printing, both optimization
  settings and borrowed/owned providers cover direct queries, original explicit
  `R.find` and guarded default roots. Checks include empty inner/outer snapshots,
  DOM order/deduplication, saved membership through writes, live hover, identity,
  detached/rootless documents, shadow/outside exclusion, nested selector exception
  ordering, input ownership and outer-document restoration. Generated text and
  linked binaries exclude Script. Refusals include missing identities, budget
  exhaustion, custom hooks, snapshot mutation/escape, helper mutation/conditional
  initialization/escape and a nullable inner receiver.
- The affected dataset case passed **28 sources, 112 Node/VM source-double
  observations, eight GCC/Clang binaries and 432 refusals**, including HTML/SVG
  and its existing lifetime sanitizer. This checks String-snapshot iteration
  after changing shared normalization and member evidence.
- Before the final member-proof change, the selected four-case run passed
  element-iteration, find-elements and dataset but failed nested iteration;
  **199.50 s**, three passed and one failed. Flat element iteration completed
  **64 native executions/116 refusals**, and indexed `R.find` completed
  **48/114**. The former nested refusal was moved unchanged into the new
  fixture as a positive lowering check under both optimization settings.
- After immutable-holder support, the two-case run passed document-default
  (**32 native executions/92 refusals**) and exposed the bounded-member default
  check in nested iteration; **90.00 s**, one passed and one failed. Those
  failures were proof boundaries, not reasons to rewrite the vendor helper.
- Earlier development gates caught a const MLIR wrapper accessor and a unit
  test using a private preparation function. Both were corrected. A fixture
  used unsupported dynamic subtraction to observe a copied length; it now
  compares lengths directly. Another tried ordinary child removal on the
  document root; it now uses the public document-root removal API. No runtime
  semantics were changed to satisfy a fixture.
- All eight final code/test SHA-256 hashes match the devbox. Scoped pinned
  formatting of five C++ files, Black/AST checks of both Python fixtures and
  `git diff --check` pass. Required `tools/format.sh --check` retains the same
  16 untouched diagnostics in `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and
  `Facts.cpp`; no repository-wide formatting pass is claimed.
- Initial availability checking inspected 73 Linux process identities and 358
  Windows CIM Name/ExecutablePath/CommandLine records, with no actual Claude
  executable, Node CLI, live loop or errors. The stopped result was journaled;
  broader browser authorization was not needed.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, broad
  sanitizer replays, local builds and push were skipped. No above-cap collection
  or new element-specific Node/VM differential execution is claimed.

## Next boundary

Custom iterator admission still needs a proved callable `Symbol.iterator`,
typed `next` results, state/effect and escape checks, and `return`/IteratorClose
behavior for abrupt completion. Add a minimal valid closed iterator fixture;
retain the existing custom-inner-iterator refusal, whose returned DOM element
does not establish a valid iterator protocol. Unknown protocols stay refused.
The importer and existing C++ loop lowering remain the seams; source semantics
must be proved before selecting a range representation.

Literal range-for printing additionally needs recovery of the source loop's
scalar state and completion behavior while retaining the proxy cap. Unguarded
default roots, general Array behavior, application initialization/driver,
overlapping DOM Map keys, original B/Data+B, object-valued fields, tagged Map
snapshots, broader Strings, conditional callees and mutable cells remain open.
Full Bootstrap is not admitted.
