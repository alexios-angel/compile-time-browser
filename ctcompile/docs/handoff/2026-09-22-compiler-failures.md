# Compiler failure repairs, 2026-09-22 UTC

Continued clean `0b950bc6` and the failures recorded by the
[full monorepo run](2026-09-21-full-monorepo-tests.md). This repair session uses
focused validation; the earlier full-suite measurements remain historical.

## Compiler fixes

- `8e5e7afc`: array-contents proof now recognizes pure `arith.constant`
  producers left by branch folding. The optimized original array-borrow
  source refused solely because of an unused Boolean constant; removing that
  constant made the same source lower. The proof retains its identity without
  treating it as a JavaScript Number or pruning either structural branch.
  Regression rows cover an unused predicate and a constant predicate whose
  other arm returns the array.
- `4d03b026`: class heritage uses insertion order when publication proof scans
  it. The pointer-keyed hash map previously changed how much work an early
  return charged. Identical inherited-chain input at budget 4441 succeeded
  in six of 20 fresh processes and exhausted the budget in 14. After the fix,
  100 fresh processes at 4441 produce identical successful IR; 100 at 4440
  refuse without publishing output.

## Test repairs

- `fa4b7e3b`: lit excludes `Inputs` directories. Discovery drops exactly 51
  non-executable fixture files, from 406 entries to 355 executable tests.
- `be3176cc`: Map ownership tests accept proved Boolean arithmetic and unused
  declared DOM inputs, while checking that only used inputs receive Map key
  evidence. An observed extra input remains a refusal.
- `58700812`: escape claims reflect proved primitive subtraction snapshots and
  bounded overwrites; the source oracle checks remain in place.
- `e0b7bc45`: exception, receiver and optimization checks use the emitted
  `js_num` and `js_boolean_t` carriers.
- `7e404231`: structural Map refusal tests retain all paths with optimization
  disabled. Formerly refused constant-call paths also execute natively against
  Node and the VM. A nullable Map round trip distinguishes stored null,
  stored undefined, a Number and a missing key.
- `c326b508`: lifetime observers pass explicit String/Number values and inspect
  their values. Generated source is not repaired by the observer adapters;
  ownership, sanitizer and forbidden-symbol checks remain active.
- `4c93c974`: the original primitive equality witnesses remain positive checks
  in the String arithmetic/coercion tests.
- `7192251e`: the mixed-return refusal checks the unspecialized function, so a
  single constant call cannot erase the alternative the test is exercising.
- `ef24ea94`: nested intrinsic index witnesses declare the String intrinsic
  they use. The complete intrinsic test passes **1336 native executions,
  1222 refusals and two mutations**, with **176 Node/VM agreements** and
  **21 explicitly retained VM casing/index differences**; it links Core only.
- `0c3a84a4`: array-borrow tests distinguish a specialized direct owner from a
  dynamic selection. The original 165-byte source is unchanged; both-call
  witnesses retain observable borrows and all copy controls. A region-local
  literal admits only after the closed call moves it into the entry block.
- `41f90d1b`: scalar-global tests assert owning String/Number and String/Boolean
  carriers. The original mixed-global helper source is positive; a BigInt
  variant retains backward call-graph refusal. Owning-field rejection checks
  its current precise diagnostic.
- `6aac8b9a`: four of the twelve original absent Map key/result cases now
  execute natively under both policies. Eight still refuse, including the
  mixed Boolean/Number read without independent payload evidence. Original
  call counts, changed repair observations, stale/fresh forged facts and
  rerun checks remain covered.
- `4944f90d`: the original post-`super` receiver-helper source now executes
  as a positive check, matching the existing borrow proof. The super-method
  rejection mutates only `mix$4`, so a constructor rejection cannot mask it.
- `8db42c8a`: the nullable short-circuit source still refuses with complete
  ownership and its original 17 calls; the test checks the precise unsupported
  owning-alternative diagnostic. The foreign-empty lifetime observer uses the
  typed `js_num` result and retains destruction, future-call and sanitizer checks.
- `c15ad804`: original conditional holder reads, omitted `charAt` indices,
  nested dataset loops and direct/repeated numeric equality execute natively.
  Parameter equality still refuses because its unused field is unproved.
- `01b76ff6`: the original Boolean numeric Map argument executes natively and
  remains subject to forged-proof checks. A 22-row probe under both policies
  confirmed that only this source admits; ten other numeric/scalar sources
  still refuse. The exact repair source remains independently covered.
- `a177fffe`: the original historical String-field comparison executes under
  both policies. Its six source calls, exact repair, native trace and fresh/stale
  forged-fact checks remain covered. The sibling Object/String payload still
  refuses with complete ownership and unchanged producer/consumer edges.
- `844c8f92`: the unchanged implicit-return source executes with
  `nullable_scalar` callable results and Map keys under both policies. Its
  original eight calls and native get/set/get/set/size operand edges remain
  checked; Node, the interpreter and standalone C++ all observe `trace=1`.

## Measured validation

All builds ran on the devbox under `/tmp/ctbrowser-devbox-build.lock`, using
explicit targets through `tools/remote-build.sh`. No browser implementation or
runtime-oracle semantics changed.

The focused CTest command was:

```sh
ctest --test-dir build -j2 --output-on-failure --no-tests=error \
  -R '^ctcompile_(escape_analysis_arrays|owned_global_shared_map|host_contract_seeded_maps)$'
```

All three pass: array contents **1.61 s**, shared Map ownership **258.39 s**,
seeded Map contracts **42.00 s**; parallel wall time **258.40 s**.

Across the saved lit JSON files, **25 of the original 27 failing cases now have
a whole-case PASS**. Class initialization and Global Map use the split checks
detailed below. These are focused results, not a fresh full-suite pass. The
exact case list and per-case timings are in `runners/ctcompile-lit-repair-summary.txt`.

The first lit repair batch selected 23 previously failing cases and completed
in **236.97 s**: **19 passed, four failed**. Those failures exposed three
remaining diagnostic/helper expectations and a region-allocation source that
default specialization can now admit. Successful tests were not replayed as a
full suite. The targeted twelve-case absence helper also passes, including
both optimization policies, native executions and its repair/forgery controls.

The eight-case long batch completed in **2196.38 s**, with **two passed and
six failed**. Intrinsic symbols and nullable globals passed. Three late source
edits had missed its sync; their hash mismatch was detected and no pass was
credited. The other failures exposed stale DOM/class expectations and a Map
diagnostic change after earlier failing assertions no longer stopped the drivers.
The next four-case batch verified **34 source hashes** and completed in
**78.57 s**, with **three passed and one failed**. Array borrows passed
**84 native executions, 24 copy controls and 37 refusals**; call-graph and
owning-field diagnostics also passed. The remaining DOM failure was a newly
admitted nested iteration source.

Required `tools/format.sh --check` still reports the same 16 pre-existing
diagnostics in `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Modified C++ and Python files pass
their scoped formatter checks.

Logs are preserved beside the monorepo in
`../test-results/2026-09-22-compiler-repairs/`. The `runners/` directory preserves
the exact shell commands, lit filters, source hash manifests and continuation
scripts. Build targets were `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-owned-global-shared-map`
and `ctcompile-test-host-contract-seeded-maps`; later test-only syncs built
`ctjs-opt ctjs-translate`. Full CTest, unfiltered compiler
lit, conformance suites, broad corpora and platform matrices were not rerun.

## Interrupted driver continuations

Detached local runners retain the build lock across SSH and survive turn
interruptions. Each stops `devbox-idle.timer` during testing and restores its
prior active state on exit. Continuations reuse only saved source/manifest/native
artifacts with matching JavaScript and complete function/owner censuses. They
resume the original driver functions; they do not count the saved prefix as
newly executed work.

The class driver completed **506 + 86 + 244 = 836 source observations** across
its original prefix and two continuations. The final continuation passed in
**485 s**, with **684 native executions, 488 unprepared refusals and 294
preparation refusals**, followed by constructed-method checks (**16 native,
20 refusals**), prototype-key checks (**four native, six refusals**) and
**13 prepared-source refusals**. Initial helper guards passed in the original
prefix. This is complete split driver coverage, not a fresh whole class lit pass.

The selected DOM class lit passes in **384.83 s**, with **668 Node/VM source
observations, eight combined native executions and 5090 refusals**.

The Global Map driver completed **413 original positive programs**, with the
new Boolean source checked separately, and earlier controls before its first
stale nullable assertion. Subsequent continuations completed short-circuit,
foreign-empty lifetime/sanitizer, numeric/scalar, size, join and mutation controls.
One temporary continuation used relative paths for saved absolute-path artifacts;
its generated-C++ comparison differed only in provenance comments. The corrected
absolute-path runner passed those controls without a repository implementation
change. The historical String-field and seeded/mixed controls also pass. The final
continuation completed in **26 s**, including the implicit Undefined result/key
source under both policies, remaining source-authority reruns and **681 further
rollback cutoffs**. This completes the Global Map driver's remaining path.
Its earlier completed prefix and continuations are recorded separately; this
is not a fresh whole Global Map lit pass.

`98e650b6` retains native Map/field access censuses for the historical
String source and the final size-result-to-`trace` edge for the implicit-return
source. The focused guard check passes in **9 s** under both optimization
policies and both C++ printing forms. It rejects **16 deliberate mutations**
that remove calls, field reads or trace assignment; the existing default
readback-helper caller also passes. No completed Map budget loop was replayed
for this test-only strengthening. After all checks, `devbox-idle.timer` was
verified **active/enabled**, and the shared build lock was available. The last
required formatter run still reports exactly the same 16 untouched diagnostics.

The next native feature boundary remains **`counted-break-exit`**: transport the
live counter across the importer's conditional break exit. Broader iterator
state, literal range-for printing, unguarded Bootstrap defaults and the application
driver remain unfinished. The earlier seven conformance crashes and two WPT event
regressions are outside this compiler repair task and remain follow-up work.
