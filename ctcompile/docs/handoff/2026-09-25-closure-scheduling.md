# Native iterator closure scheduling and nested inventory

Resumed clean `ctcompile-v1` at `dcec1bd6` from the callable-continuations
handoff. The old WIP branch was already an ancestor. An interruption stopped
this iteration after the trace target built; its exact CTest resumed without
rebuilding. Both replacement agents later hit rate limits. The parent finished
the preserved direct-ordering tests and escape inventory candidate, validation
and review. No independent final signoff is claimed.

## Landed

`f8482c73` extends `DOMSource::precedesInStructuredBody` to Invoke normal/unwind
and Try catch ancestry. The trace found both original close-helper calls under
Invoke → If → If → Invoke → Invoke → Invoke → Try, with an earlier closure and
an initial capture value. Direct Invoke bodies still allow only Call/CallDirect.
The six shared callers retain their complete use, immutable capture, receiver,
field-write, callback and effect checks. A capture assignment must precede the
whole enclosing operation. Fourteen direct controls check ordering, sibling and
unknown-region refusals, unchanged IR, and exact/short/zero ancestry budgets.

Both original `caught-node-getter-identity` and `caught-node-method-identity`
JavaScript bodies now prepare, lower, compile and execute unchanged. The native
code uses existing DOM APIs and ordinary borrowed element references. The
source node catch needs no C++ exception or runtime value model. Real-document
checks cover exhausted/nonexhausted iterators and absent/present `data-visited`
on two documents, over 16 document lifetimes per binary. They assert returned
Boolean values, write order/count/node identity and the resulting attributes.
Node and the interpreter independently retain the original two-state read/write
trace. Both providers, optimization settings, printers and GCC/Clang pass;
zero-budget refusals remain.

`840c07dc` reuses the escape post-pass's descendant walk to inventory all tracked
sites under live root CFG blocks. Nested sites stay UnknownOp with no operand
witness, after whole-function raw-frame refusals. Consumers therefore cannot
mistake inventory for a Stored operand and discharge it. Dead root blocks count
all descendant sites; dead nested arms remain conservatively inventoried. The
arguments placement check compares each site's root ancestor. Existing storage
and load censuses remain incomplete for regions; structured retention still
rejects nested allocation origins. The historical fixture now inventories two
arrays instead of one but gains no confinement. Seven new controls check loops,
dead branches/CFG blocks, arguments ordering, suspension and missing lattices.

## Exact focused validation

All remote work held `/tmp/ctbrowser-devbox-build.lock`. Explicit targets:
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`, `ctjs-opt`,
`ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-completion` and
`ctcompile-test-escape-analysis-arrays`.

- `ctcompile_exception_recovery`: **1/1, 5.32 s** (5.33 s total), including fresh
  complete DOM proof for both originals and all existing protocol/lifetime
  mutation and rollback checks.
- `ctcompile_host_contract`: **1/1, 2.71 s** (2.72 s total). Native production
  did not change afterward; the test was not replayed.
- `ctcompile_escape_analysis_completion`: **1/1, 0.01 s** (0.02 s total).
- `ctcompile_escape_analysis_arrays`: **1/1, 4.69 s** (4.70 s total).
- `native-selection.py` completed `invocation-return`, `write-boolean-snapshot`,
  `helper-next-record` and all existing refusal controls before its old getter
  refusal assertion failed on newly emitted native code. Those completed checks
  were not repeated. Final `outer-selection.py` ran only the two changed original
  cases and their new native execution/zero-budget checks: **PASS**.
- Across those selections: **40** generated C++ files, **80** GCC/Clang binaries.
  Harness binary namespace checks pass; independent generated-source checks
  reject `ctbrowser::script`, `ctbrowser::aot` and `ctjs::`. All **seven** final
  implementation/test hashes match the devbox.
- `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**. The
  subsequent one-line escape expectation correction passes scoped clang-format.
  `git diff --check` passes. All **10** original DOMURI raw literals and **72**
  Python newline-bearing literal values remain unchanged; the latter count
  includes the two escaped-newline original sources. One native assertion
  literal was added. Historical escape witness bodies remain unchanged.

The trace CTest passed in 5.32 s before production changed. The first new recovery
run failed its two obsolete refusal expectations because preparation succeeded.
The first source selector failed its obsolete getter refusal for the same reason.
The initial escape completion run expected zero missing-lattice operands, but
the existing root `truthy` parameter correctly counts one; the expectation was
fixed. Its failing combined script never reached arrays. A build heredoc's SSH
consumed the following CTest command once; that build-only result was never
counted as a test pass, and tests ran separately with stdin detached. All these
logs are retained.

Fresh process checks examined 13 Linux identities and 342 Windows
Get-CimInstance processes, finding no Claude match. Sixty Linux permission
failures made availability uncertain, so concurrent-agent restrictions applied.
No browser/runtime/shared implementation changed. No local C++ build, push,
history rewrite or other branch edits.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers
and performance. These focused checks are not a full-suite pass.

## Next boundary

The two original structured JavaScript iterator witnesses are now native.
Their raw-helper C++ controls remain refused at `DOM iterator getter requires a
terminal throw` and `DOM Symbol.iterator must be a closed identity method`.
Continue at `testObservedIteratorRecovery` in `ExceptionRecovery/Completion/DOMURI.cpp`:
compare its raw and structured helper bodies, normalize the raw CFGs before
custom-iterator identity/getter proof, and preserve effects and transactional
rollback. Do not weaken the handler/close/lifetime proof to bypass that work.

Nested sites now have a conservative inventory, but distinct dynamic instances,
region lifetime and ownership remain unproved. Mutable exceptional state,
escaping owners, unguarded Bootstrap defaults, the native application driver and
full native Bootstrap remain unfinished. No full-Bootstrap admission is claimed.

Evidence: `../test-results/2026-09-25-closure-scheduling` relative to the monorepo,
including focused logs, both source selectors, source artifacts, parent review,
preservation/hash reports and verified `SHA256SUMS`.
