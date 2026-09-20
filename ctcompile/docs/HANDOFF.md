# Handoff: continuing ctcompile

Synchronization now uses `~/Downloads/claude/AGENT-SYNC.jsonl`: read its `PROTOCOL`
records, current claims and recent `JOURNAL` records before editing. The shared
`~/Downloads/claude/agent-sync.py` writer appends locked JSON records. This format
migration preserved history. The latest native work and measured next boundary
are recorded below.

> NOTE (Claude, 2026-09-16): `ctcompile-v1` history was **reworded** while Codex
> was stopped - every unpushed commit from `f7966251` (origin) forward now has a
> `ctcompile(<area>): ...` message, but **the trees are byte-identical**, only
> messages and SHAs changed. The browser rounds 2-5 and five security fixes are
> integrated at the current tip. Pre-reword tips are kept as
> `ctcompile-v1-backup-premsg2` / `-premsg`. Full detail is in the
> `SESSION HANDOFF` journal in the history referenced by
> `~/Downloads/claude/AGENT-SYNC.jsonl`. Just branch from the current `ctcompile-v1`
> tip - nothing about the native work changed.

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## String value carrier, 2026-09-20 UTC

Continued clean **2bc6cc6d**. **8cc0cf6e** introduces owning
`js_basic_string<char>` / `js_string`, exact const methods and String exceptions.
**ac3cdaa2** carries String literals, concatenation, calls/captures and fields in
that class; generated prefix calls use `.startsWith(...)`. Map/vector/nullable/
JSON storage and public browser operations retain explicit raw text adapters.
**98786531** updates owning C++ clients and restores pinned formatting.
No source admission or browser/VM behavior changed.

Measured focused checks: runtime CTest **1/1**, **17 distinct lit cases**, and
**eight admitted ownership cases** passed across corrected runs. Six selected
class cases passed **24 Node/interpreter observations, eight native executions
and 118 refusals**. A dynamic nested Map control remains refused, with two source
observations and six distinguishing mutations. All **43** code/test hashes match
the devbox; scoped formatting/syntax passes. Global formatting retains 16
pre-existing diagnostics. Full suites and wtfjs replay were skipped.
[Exact changes, checks and intermediate failures](handoff/2026-09-20-native-string-carrier.md).

**Next:** proved primitive String numeric conversion and mixed String/Number
addition, beginning with runtime `baNaNa` and negative controls. General String
length still uses bytes (ND-1); existing admitted DOM UTF-16/casing paths retain
their separately recorded VM differences. Resolve that alignment separately.
Raw Number alias retirement, collections/document views and the planned
`Symbol.hasInstance` wrapper remain. Indexed Bootstrap `R.find` still needs the
NodeList >1,000,000 undefined-slot boundary resolved, preserving the independent
2^24 spread cap. Full Bootstrap and the application driver remain unfinished.

## Number value carrier and semantic edge-case plan, 2026-09-20 UTC

Continued clean **5d084ecf**. **dbc82d9a** preserves typed Number values through
const qualification, deduction and exceptions. **25472dd7** migrates Number
literals, arithmetic, comparisons, fields, calls, captures and browser counts to
`ctnative::js_num`. Map/vector/JSON storage and public Core/C formatting keep
explicit binary64 adapters; Map SameValueZero is unchanged. **ae8124d6** updates
15 further signature/shape checks and retains their intended runtime proof paths.
No source admission or browser/VM behavior changed.

Measured focused validation: runtime CTest **1/1**, **36 distinct selected lit
cases** and **8 selected ownership cases** passed across corrected runs. All
**62** final code/test hashes matched the devbox. Scoped formatting/syntax passes;
required global formatting retains the same 16 pre-existing diagnostics. Full
CTest/lit, broad corpus/matrix and WPT/test262 were skipped.
[Exact changes, checks and intermediate failures](handoff/2026-09-20-native-number-carrier.md).

The user's wtfjs reference now has a [semantic edge-case plan](plans/native-js-semantics.md),
pinned to its README revision. It assigns missing coercion/equality, array,
prototype, evaluation-order, parser and host work to the correct layer. No wtfjs
corpus has been executed or claimed supported.

**Next:** implement `js_basic_string<char>` / `js_string` with an admitted String
operation group and primitive-coercion witnesses. Retire the raw global
`js_num = double` alias only with its remaining printer/storage clients. Preserve
the scheduled `Symbol.hasInstance` / equivalent `std::holds_alternative` wrapper.
Before indexed Bootstrap `R.find`, resolve NodeList slots above 1,000,000 returning
undefined in Shell; retain the separate 2^24 spread cap. Full Bootstrap and the
application driver remain unfinished.

## Typed Number conversions and NaN, 2026-09-20 UTC

**5c2f4332** implements `js_nan_t` and makes Boolean/nullable numeric conversions
return `ctnative::js_num`. Undefined converts to a Number NaN; null/false to
positive zero; numeric payloads retain signed zero. Lowering explicitly extracts
`.value()` for existing f64 arithmetic/storage. Exact Number extraction remains
pure for dead-code pruning and Map snapshot fusion; unknown members remain
barriers. **30e9e3c7** prints native binary64 NaN literals through the token.

Focused checks pass: runtime CTest **1/1**, **10 distinct lit cases** and
**4 selected ownership cases**, including existing differential, mutation and
sanitizer checks. The new NaN test's missing type-pin macro was fixed before its
successful rerun. All **13** code/test hashes matched the devbox. Scoped formatting
passes; global formatting retains the same 16 pre-existing diagnostics. Full
suites were skipped. [Exact changes and validation](handoff/2026-09-20-native-number-coercions.md).

**Next:** migrate the Number value carrier in `LoweringSupport.cpp::carrierType`,
remaining literals/arithmetic/math and call/capture signatures together, with
explicit Map/vector/JSON storage adapters. Then introduce `js_basic_string<char>`
and `js_string` using public Core string algorithms. Preserve the existing
`instanceof`/`Symbol.hasInstance` schedule and NodeList >1,000,000 Bootstrap boundary.

## Basic Number class and default type aliases, 2026-09-20 UTC

**21b2dee2** names the planned templates `ctnative::js_basic_num<T>` and
`ctnative::js_basic_string<T>`, with default aliases `js_num` for `double` and
`js_string` for `char`. **bb265540** implements the Number class and uses it for
numeric global observations, extracting `.value()` before C formatting.
Existing arithmetic, Maps and capture signatures remain binary64; generated
declarations inside `ctnative` qualify their compatibility alias as `::js_num`.

Focused validation passed: runtime CTest **1/1**, **9 selected lit cases** and
**8 selected ownership cases**, including existing differential, mutation and
sanitizer checks. All **20** code/test hashes matched the devbox. Scoped formatting
passes; global formatting retains the same 16 pre-existing diagnostics. Full
suites were skipped. [Implementation and exact checks](handoff/2026-09-20-native-number.md).

**Next:** Number literals, conversions, arithmetic and signature adoption, then
String and collections/document views. Add the user's planned `js_nan_t` explicit
Number-NaN construction token with numeric literals/conversions; it is not yet
implemented and must preserve Number semantics and distinguish absence. Keep the
typed `instanceof`/`Symbol.hasInstance` schedule with class/prototype proofs.
NodeList slots above 1,000,000 remain the next indexed Bootstrap boundary.

## JavaScript Boolean carrier migration, 2026-09-20 UTC

**92024490** continued **30a74866** with `ctnative::js_boolean_t` across native values,
signatures, comparisons, Maps, captures and browser result adapters. C++ control
conditions and public JSON storage retain `bool`. No VM or source-proof change.
Focused validation: runtime CTest **1/1**, **17 distinct lit cases** and **11
selected ownership cases** passed across corrected runs, including their existing
sanitizer checks. All 35 code/test hashes matched the devbox. Global formatting
retains 16 pre-existing diagnostics; scoped checks pass. Full suites were skipped.
[Implementation, exact focused validation and remaining boundaries](handoff/2026-09-20-native-boolean.md).

Next migrate Number/String carriers, then collections and document views. The
user's typed `instanceof` wrapper is scheduled with class/prototype proofs:
invoke a proved `Symbol.hasInstance` hook, otherwise use `std::holds_alternative`
where equivalent. Preserve receiver, inheritance, effects and exceptions.
NodeList slots above 1,000,000 remain the next indexed Bootstrap boundary.

## Typed JavaScript interface implementation, 2026-09-20 UTC

Resumed the first implementation milestone from **8ab0394c** and the design
below; the starting tree was clean and no interrupted branch needed landing.
**1576f18f** composes the existing four selector method objects under
`ctnative::Element.prototype`. Generated calls now use, for example,
`ctnative::Element.prototype.querySelector.call(element, styles, selector)`.
The old flat names reference those same immutable objects. Public Style behavior,
explicit borrowed inputs and all JavaScript identity/mutation proofs remain in
place. No dynamic lookup, allocation or virtual dispatch was added.

**bd727da7** introduces distinct `ctnative::undefined_t` and
`ctnative::js_null_t` construction tokens. The shared `absentConstant` emitter
constructs `nullable_scalar` from them for literals, initial scalar storage,
Map.clear results and used void DOM results. Default construction and `.null()`
remain compatible. Tags, payload, calls/returns and optional joins retain the
existing carrier; both absence literals still infer `Opt<Bottom>`. This starts
the primitive migration; exact standalone token signatures are not implemented.

Selector validation passed: exact `ctcompile_native_runtime` **1/1** (0.02s
CTest total), and closest/query/query-all/prototype-query/spread-length lit
**5/5** (103.22s). All seven changed code/test hashes matched the devbox before
**1576f18f**. The selector tests compile/run against public DOM/Core/Style and
check generated source and linked symbols for Script/AOT dependencies; they
are not VM/browser end-to-end differential measurements.

Absence-type validation passed: explicit targets built, runtime CTest **1/1**
(0.02s total), both optional-scalars/scalar-unions differential fixture lit
cases, and native-dom action lit. The fixtures compare generated observations
with the interpreter, compile both printing layouts under GCC/Clang, audit
symbols and exercise negative mutation/type-pin controls. Two raw lowering
checks initially failed because default precomputation erased their intended
unions (**3 passes / 2 failures**, 245.62s for that selected run). Requesting
`optimize=false` preserves every JavaScript body and refusal expectation;
the corrected two-case raw rerun passed **2/2** (0.22s), with no rebuild needed.
All **11** final changed code/test SHA-256 hashes match the devbox. Parallel
review found no additional issue in the constructors, callers or refusal paths.

Exact validation commands, local build then devbox test commands, all serialized
under `/tmp/ctbrowser-devbox-build.lock`:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(closest|query|query-all|prototype-query|spread-length)[.]test$'
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/((Lowering/Scalars/(optional-scalars|scalar-unions)[.]mlir)|(Fixtures/Scalars/(optional-scalars|scalar-unions)[.]test)|(Browser/native-dom[.]test))$'
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/Scalars/(optional-scalars|scalar-unions)[.]mlir$'
```

Changed C++ formatting, Python Black/syntax and whitespace checks pass.
Required `tools/format.sh --check` reports the same **16** pre-existing errors
in four untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and
`Symbolic/Facts.cpp`. No full CTest/lit, broad corpus/matrix, WPT/test262 or
sanitizer run was requested or performed. Browser code and the VM oracle are
unchanged. Process checks found no Claude identity in 70 Linux / 354 Windows
processes, but 57 Linux executable paths were unreadable; status was treated as
uncertain and work stayed in ctcompile and the requested external plan.

**Next type boundary:** introduce `js_boolean_t` while separating JavaScript
Boolean values from C++ `bool` control-flow conditions. The current carrier,
SCF i1, browser results and callable/shape spellings share `bool`; migrate
Boolean literals, comparisons, signatures, optional conversions and printing
together. Then migrate Number/String and collection/document views. The global
`using js_num = double` still needs a deliberate qualified-template migration.

**Next browser boundary:** reconcile native NodeList indexed slots above
1,000,000 with Shell returning undefined before admitting indexed `R.find`
consumers. Keep the separate 2^24 spread cap and current count-only integration.
Default document roots, Object/Array prototypes, BigInt/Symbol, full Bootstrap
and the application driver remain unfinished.

## Typed JavaScript interface plan, 2026-09-20 UTC

The user requested purpose-built native C++ types with JavaScript methods,
operators and `Element.prototype.*`, `Object.prototype.*`, `Array.prototype.*`
syntax. [The maintained design](plans/native-js-types.md) records the exact
vocabulary, primitive versus identity semantics, null/undefined distinction,
String encoding rules, operator limitations, typed prototype objects and
borrowed document/element views. Standard containers remain internal storage;
all browser behavior uses ctbrowser's public subsystems. No universal runtime
value, collector or reference-counted object graph is introduced.

External master-plan parts **00, 01, 24 and 25** now point to this design.
Part 24's future-facing type table, array methods, union semantics and prototype
rules reflect it; part 25 retains owner/alias/effect proof requirements. Dated
measurements and historical implementation examples are preserved. Native DOM
documentation distinguishes the current `.call` interface from the planned one.
These are documentation changes only: no runtime class, emitter behavior,
source admission or measured native coverage changed.

**Next implementation:** compose `Element.prototype` from the existing four
selector method objects and migrate the exact proven EmitC callees/type checks,
retaining explicit Style input initially. Then introduce the qualified
`ctnative::js_num<double>` and other primitive classes in coherent batches,
with their literals, signatures, conversions, optional joins and type pins.
The current global `using js_num = double` cannot be renamed to a template
blindly. Collections/closed shapes and document views follow; BigInt needs
public-core extraction and Symbol needs a separate identity/registry proof.

Carry forward the existing NodeList indexing defect: above 1,000,000 the VM
returns undefined, while direct native indexing currently supplies an element.
Resolve that before adding indexed `R.find` consumers; retain the separate
2^24 spread cap. Default document roots, full Bootstrap and the application
driver remain unfinished. Do not change the runtime oracle to match new wrappers.

Validation passed: 14 requested type/prototype names, seven document fence
balances, seven design links, six master-plan consistency assertions and
whitespace; parallel read-only review found no blocking issues. The required
formatter reports 16 pre-existing diagnostics in four untouched files. Build,
CTest, lit, differential, corpus/matrix, WPT/test262 and sanitizer checks were
skipped for this documentation-only change; no full-suite pass is claimed. The process check
found no Windows Claude identity among 365 processes; Linux executable reads
were denied for 57 processes, so availability was treated as uncertain. Work
stayed in compiler docs and the explicitly requested external plan; no browser
or shared-code edits, no broader authorization used, and no push.

## Bootstrap R.find count integration, 2026-09-20 UTC

Resumed the explicit-element `R.find` boundary recorded below and in the master
plan; the starting tree at **987dafbe** was clean. Linux `/proc` and Windows
`Get-CimInstance Win32_Process` checks found no Claude executable, CLI or loop
(65 Linux / 362 Windows processes, no unknowns); this was journaled before work.
Parallel inspection traced the Shell/VM limits; after delegated agents hit their
rate limit, Codex completed the implementation and native tests under its claims.

**323fea4c** proves the exact imported NodeList spread into an empty concat
receiver when the result has only `.length` observations. It retains the public
Style query and emits a scalar minimum with **16,777,216**, the VM's proxy-spread
cap. Original Array concat/species and absent receiver/element spreadability hooks
are explicit embedding guarantees. Every source use, loop state and copied slot
is checked; mutation, extra arguments, result indexing/identity/escape, stale
fingerprints and exhausted budgets refuse without publishing partial changes.
**a002f180** extends existing element guards to strict equality with `undefined`
in either operand order. This removes the original helper's unused
`document.documentElement` default only for an explicit validated element.
Unproved cells, joins and nullable query results receive no new element facts.

**454cfc3e** runs normalization after helper/default-guard expansion.
Its source-pinned fixture calls the unchanged Bootstrap `find` arrow from a local
holder and observes its count across DOM mutations. Direct spread tests also
exercise scope/root/shadow boundaries, detached roots, invalid-selector effect
order, and borrowed/owned document validation. Generated C++ uses existing
`ctnative::querySelectorAll.call` and links DOM/Core/Style; no VM, GC or new
native runtime carrier was added. Browser files are unchanged. Default document
roots and indexed concat consumers remain refused.

Final focused validation passed: host CTest **1/1** (0.55s), both existing
selector lit cases, and corrected spread-length lit **1/1** (**75.54s**). The new
case reports **32 native executions / 52 expected refusals** across borrowed and
owned providers, GCC/Clang, both optimization modes and both printing layouts.
Generated source and linked binaries pass Script/AOT symbol audits. All **11**
final code/test source hashes match the devbox; the final synchronization needed
no compilation. A whitespace-only fixture cleanup after its initial sync did
not change JavaScript tokens or test behavior.

Commands ran under `/tmp/ctbrowser-devbox-build.lock`, with helper and SSH stdin
from `/dev/null`. From the local checkout and then the devbox project directory:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(spread-length|prototype-query|query-all)[.]test$'
# Test-only iterations synchronized with these two explicit targets:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-spread-length[.]test$'
```

The first build caught ambiguous MLIR dominance overloads; explicit operation
pointers fixed them. The next host check caught a NumberAttr bit-pattern error;
using the encoded double fixed the cap before the first commit. Initial source
fixtures exposed existing same-block helper restrictions, then the missing
parameter guard above. The final production build passed host CTest **1/1**
(**0.55s**) and both existing selector lit cases; the new case then exposed only
a duplicate local name in the owned test client (three-case run **81.76s**).
The corrected client scopes its owned checks independently. Earlier the core
normalizer also passed host CTest **1/1** (0.53s) and both existing selector cases
(three-case run 79.03s, new fixture failed before native execution).

Required `tools/format.sh --check` retains **16** pre-existing diagnostics in
four untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h`, and
`Symbolic/Facts.cpp`. Changed C++/Python formatting and whitespace checks pass.
These are standalone native API checks and symbol audits, not VM/browser
end-to-end differential observations. Full CTest/compiler lit, broad corpus/matrix,
WPT/test262 and sanitizers were skipped. No push.

**Exact next boundary:** reconcile direct query-all indexed reads with Shell's
`collections.cpp:584` limit: indices above **1,000,000** return `undefined` in
the oracle, while the existing native indexed path returns an element. Before
admitting indexed `R.find` consumers, prove a distinct element-or-undefined slot
and guard its dereference; preserve the separate **2^24** spread cap from
`Script/vm/call/invoke.cpp:363`. Current count-only lowering does not observe
slot values and retains their contribution to length. Large collections were
not allocated in these tests; the cap and branch direction are checked in raw
IR proof tests. Default document roots need a separate document/session identity,
nullable-root and Style contract. Full Bootstrap initialization, retained events
and the application driver remain unfinished.

## String-to-number lookup cleanup, 2026-09-20 UTC

**38e51ca6** replaces `classIntrinsicArity` and `iteratorIntrinsicArity` branches
with `llvm::StringMap<unsigned>` lookups. **313fa8ef** converts six Map-method
arity expressions in native analysis, host preparation and closure lifting;
**cd029817** converts the leaf-object test call-count selector to `dict.get`.
The scan found **nine** matching sites across **eight** files. Each table retains
its original accepted names, fallback (0, 1, 7 or 99) and bounded string work.
No shared policy API, browser/oracle changes or generated LLVM dependency.

The explicit devbox build and **4/4** selected CTests passed (281.97s). Four
selected Map/object lit cases passed; the fifth, class initialization, stopped
at a stale preparation expectation (selected lit run 455.65s).
Earlier **622f1ca8** already allowed preparing the read-only helper in
`captured-holder-receiver-escape`, while native method forwarding still refuses.
**dea36b1f** moves that control to `PREPARED_ONLY`, preserves its construction
count and checks the exact `this`-argument native refusal in both modes. Source
bytes and Node/interpreter observations remain unchanged.

After correction, **12/12** selected existing class controls passed (30.36s):
12 source observations, 32 native executions, 24 unprepared refusals, 12
preparation refusals and four prepared native refusals. The same driver also
passed its constructed-method checks (16 executions/20 refusals) and original
`r` guards (eight executions/four refusals). The entire class aggregate was
**not replayed**. All **ten** final code/test source hashes match the devbox.
The Python metadata change also passed a before/after comparison of source,
refusal, call/function/field metadata and string literals.

Commands below ran under `/tmp/ctbrowser-devbox-build.lock`; local helper and
non-heredoc SSH commands used stdin from `/dev/null`:

```sh
# Local helper, initial build:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-host-contract-seeded-maps ctcompile-test-exception-recovery ctcompile-test-owned-global-shared-map ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_(host_contract|host_contract_seeded_maps|exception_recovery|owned_global_shared_map)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/(Maps/(maps[.]mlir|map-proof[.]mlir|map-iterators[.]test)|Objects/(object-fields|class-initialization)[.]mlir)$'
# Local helper after the test-only correction (no compilation needed):
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
```

The final focused driver invocation ran on the devbox against the split source
fixtures left by that lit case. Only its observation selection changed:

```python
import sys
import time
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path('ctcompile/test').resolve()))
from CTNative.Lowering.Objects import class_initialization as test
names = (
    'captured-holder-receiver-escape', 'captured-holder-unused',
    'captured-holder-receiver', 'inherited-own-fields-iterate-borrow-store',
    'inherited-own-fields-iterate-borrow-return',
    'inherited-own-fields-iterate-borrow-write', 'class-map-direct',
    'class-map-helper-holder', 'class-map-record-direct', 'class-map-record-clear',
    'class-map-member-replaced', 'class-map-stored-receiver',
)
test.OBSERVATIONS = {name: test.OBSERVATIONS[name] for name in names}
sys.argv = [test.__file__,
    '--translate', 'build/ctcompile/tools/ctjs-translate/ctjs-translate',
    '--opt', 'build/ctcompile/tools/ctjs-opt/ctjs-opt',
    '--node', '/home/ubuntu/tools/node-v26.8.1/bin/node',
    '--reference', 'build/ctcompile/test/ctcompile-test-native-reference',
    '--fixtures', 'build/ctcompile/test/CTNative/Lowering/Objects/Output/class-initialization.mlir.tmp',
    '--work', 'build/ctcompile/test/CTNative/Lowering/Objects/Output/class-initialization.arity-focused',
]
print('Selected class controls: ' + ', '.join(names), flush=True)
start = time.monotonic()
test.main()
print(f'Focused class controls passed in {time.monotonic() - start:.2f}s', flush=True)
```

Required formatting still reports **16** pre-existing diagnostics in the same
four untouched files listed below. Changed C++/Python formatting and whitespace
checks pass. Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and
sanitizers were skipped; no full-suite pass is claimed. No push.
**Next native integration boundary remains:** confined Bootstrap `R.find`
spread/concat with an explicit element, followed by separately proved document
roots and ownership. Full Bootstrap initialization and the application driver
remain unfinished.

## Literal membership cleanup, 2026-09-20 UTC

Converted **27** same-subject literal equality chains of four or more comparisons
across **26** files: 16 compiler string checks, nine test-case checks and two
runtime/reference character checks. Compiler names use bounded `StringSet::contains`;
integer cases use `DenseSet`, the LLVM-independent inventory uses `unordered_set`,
and punctuation uses allocation-free `string_view::contains`. Shared Map-action,
callback-binding and closure-metadata predicates reuse existing support files.
Shorter chains, enum/SSA comparisons and source fixture contents are unchanged.
The broader C++/Python/CMake/shell scan found no additional qualifying implementation
chains; review verified all 17 raw fixture literals remain byte-identical.

Landed **c223d8cf** (Map actions), **e84eca66** (compiler classifications),
**080ec1e1** (host reserved names), **22d40e13** (test case selection), and
**451339df** (runtime/reference punctuation). No browser semantics or source-proof
authority changed; native output remains independent of LLVM and Script.

Final devbox build passed, followed by **6/6** selected CTests (7.13s) and **8/8**
selected lit cases (7.08s). All **26** final source hashes match. The first build
caught a removed local `subtract` flag still used later in an induction test;
retaining that flag with its set-derived value fixed it. No tests ran in that
first attempt. Both attempts used these commands under the build lock, with
helper/SSH stdin from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-inventories ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-test-exception-recovery ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays ctcompile-test-type-inference ctcompile-native-pipeline-strings
# On devbox, from projects/compile-time-browser (reached in the second attempt):
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_(inventories|native_runtime|exception_recovery|host_contract|escape_analysis_arrays|type_inference)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (Target/Cpp/const-bindings[.]mlir|CTJS/Transforms/resolve-globals[.]mlir|CTNative/PartialEvaluation/heap-evaluation[.]mlir|CTNative/Lowering/Maps/(map-presence-proof|object-key-proof)[.]mlir|CTNative/HostContract/(prefix[.]test|Provider/mutations[.]test)|CTNative/Fixtures/Scalars/string[.]test)$'
```

Required `tools/format.sh --check` reports **16** existing diagnostics in four
untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and `Symbolic/Facts.cpp`.
Formatting the two affected files that previously failed removed four old
diagnostics. All changed files pass scoped formatting and whitespace checks.
Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were
skipped. No push. **Next integration boundary remains:** confined Bootstrap
`R.find` spread/concat with an explicit element, then separately proved document
roots and ownership. Full Bootstrap initialization and the application driver
remain unfinished.

## Shared DOM intrinsic membership, 2026-09-20 UTC

**65273121** replaces the duplicated name-comparison chains in `Contract.cpp`
and `DOMEntry.cpp` with one `llvm::StringSet<>` and `.contains()`. The table is
shared through existing host-contract support. The same 13 names, duplicate
rejection and parser-only class-helper/Error exceptions remain; names longer
than 23 bytes refuse before hashing, and the typed count bound follows set size.
No browser semantics, emitted calls or source-proof authority changed.

Final `.contains()` sources built on the devbox and passed host-contract CTest
**1/1** (0.52s total); all **three** final source hashes match. Before the equivalent
lookup-spelling change, the shared StringSet passed host CTest **1/1** (0.52s)
and prototype-selector lit **1/1** (78.45s), with **32 native executions / 62
refusals**. An uncommitted map draft also passed the same focused checks (host
0.51s, prototype 77.88s); the final implementation follows the user's StringSet
and contains refinements. No native replay was needed for the spelling change.

Commands under `/tmp/ctbrowser-devbox-build.lock`, helper/SSH stdin from `/dev/null`:

```sh
# Local helper for each draft and the final sources:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
# Map and StringSet drafts, before the final contains spelling:
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-prototype-query[.]test$'
```

Required formatting retains **20** baseline diagnostics in six untouched files;
changed C++ formatting and whitespace pass. Full CTest/compiler lit, broad
corpus/matrix, WPT/test262 and sanitizers were skipped. No push.
**Next integration boundary remains:** confined Bootstrap `R.find` spread/concat
with an explicit element; default document roots need their own ownership contract.
The detailed proof requirements and unfinished application work follow below.

## Original prototype selector calls, 2026-09-20 UTC

**3d701f64** proves Bootstrap's original
`Element.prototype.querySelector.call(element, selector)` and
`querySelectorAll.call(element, selector)` for explicit nonnull element and
String inputs. DOM manifests supply `"initial_intrinsics": ["Element", "Function"]`.
These guarantee original global/prototype/selector identities and the original
Function.prototype.call chain without shadows or accessors. Saved constructor/prototype/method
aliases retain those identities; mutation, escape, reentry, detached calls,
wrong arity, coercion and unguarded nullable receivers refuse.

The compiler erases the proved lookup chain and reuses the existing C++ selector
method objects. Argument zero supplies the receiver's document and live Style
engine, including guarded selector results and checked snapshot members. Raw-IR
checks cover exact evidence, source fingerprints and every insufficient work
budget; incomplete proofs publish no global/prototype/method/call evidence.
No browser code, runtime semantics or ownership changed.

Measured on the devbox: host-contract CTest **1/1** (0.51s total), existing direct
query/query-all lit cases passed, final prototype lit **1/1** (77.12s). The new
case executed **32 native clients / 62 refusals**, with both providers, policies,
printing layouts and GCC/Clang. Existing direct cases add 32 executions/58
refusals. All **12** code/test hashes matched before commit. These are standalone
DOM/Core/Style clients and Script/AOT symbol audits; this new driver does not run
VM/browser differential observations.

Exact commands under `/tmp/ctbrowser-devbox-build.lock`, with helper/SSH stdin
from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(prototype-query|query|query-all)[.]test$'
# After the test-only correction, local helper then devbox:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-prototype-query[.]test$'
```

The first prototype fixture used unsupported Number strict equality in its
zero-length witness. Replacing `children.length === 0` with `!children.length`
retained the receiver-sensitive observable check using existing Number negation;
no production proof was widened. Required formatting retains **20** pre-existing
diagnostics in six untouched files; all changed C++/Python, syntax and whitespace
checks pass. Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and
sanitizers were skipped. No push.

**b8b5c3e2** replaces the requested enum-to-callee switch in `EmitC/DOM.cpp`
with an LLVM DenseMap of 17 names. Optional-String attribute/Number overloads
remain explicit type checks; specialized methods still require their own lowering.
This table is compiler-only; generated calls and argument order are unchanged.

**3b0336d1** fixes two stale String-test refusals after the earlier `c5bcd3de`
loop proof. Entry/helper attribute-mutation loops now have positive source/native
checks, including actual attribute absence, and unsafe-call loop variants still
refuse. Eight Node/VM observations were added; binary and refusal counts did not
change. A focused devbox lowering of the original helper fixture confirmed its
existing admission before the test update. No production loop proof changed.

The map's first build caught a local iterator name collision, fixed by naming
its lookup `calleeName`. The next gate passed class-list/prototype lit but stopped
at the stale String control after its positive comparisons. Final String lit
passed **1/1** (150.19s): **809 Node/VM observations**, **8 GCC/Clang binaries**,
**1,000 source refusal checks** and **463** additional provenance/depth/budget
checks. Its replacement subcheck also reported **11** source observations and
**4** native executions. All **14** final session code/test hashes match the
devbox. Final required formatting retains the same 20 baseline diagnostics;
changed formatting, Python syntax and whitespace checks pass.

Exact map/test followup commands, under the same build lock and stdin rules:

```sh
# Local helper, used for both map attempts and the test-only rerun:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser, after the name correction:
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(strings|class-list|prototype-query)[.]test$'
# After correcting the two stale loop controls:
~/.lit-venv/bin/lit -av build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-strings[.]test$'
```

**Next exact boundary:** Bootstrap `R.find` at `bootstrap.bundle.js:341` still
wraps the proved call in `[].concat(...nodes)`. Start with an explicit element
and confined local length/indexed consumption. Reuse the element vector only
after proving original Array concat/species and absent element
`@@isConcatSpreadable` hooks. The current VM materializes proxy iterables with
a **2^24-member cap** (`Script/vm/call/invoke.cpp`); preserve it in spread
conversion without changing direct query-all length. Default
`document.documentElement` needs a separate document/session identity, nullable
root and Style contract. Constructor publication/nested Map lifetimes, retained
events, full Bootstrap initialization and the application driver remain unfinished.

## C++ selector method objects, 2026-09-20 UTC

**63b9c4d2** implements the requested object-oriented selector interface in
`Runtime/ctnative.hpp`. `matches`, `closest`, `querySelector` and
`querySelectorAll` are `inline constexpr` instances of stateless classes with
`const` templated `call` members. Generated C++ now spells, for example,
`ctnative::querySelector.call(element, styles, selector)`. The original public
Style calls moved into those methods; ownership, validation, exception order
and source proofs are unchanged. There is no callable table, virtual dispatch,
VM context or GC handle. Regenerate older emitted selector sources for the
new runtime helper spelling.

Focused devbox checks passed on the first gate: native runtime CTest **1/1**
(0.02s total), selector lit **3/3** (54.28s), comprising **48 native executions /
84 refusals** across both providers/policies/layouts and GCC/Clang. The existing
tests now require the `.call` spelling; generated clients retain the Script/AOT
symbol checks. All **five** final code/test hashes match the devbox.

Commands under `/tmp/ctbrowser-devbox-build.lock`, with helper/SSH stdin from
`/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(closest|query|query-all)[.]test$'
```

Required formatting still reports **20** pre-existing diagnostics in six
untouched files; changed C++/Python, syntax and whitespace checks pass. Full
CTest/compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were skipped.
No browser/oracle changes or push. **Next:** prove original Bootstrap's
JavaScript `Element.prototype.querySelector(All).call` identities; the readable
C++ member spelling does not grant that source proof. NodeList spread, document
roots, constructor publication/nested Map lifetimes and the application driver
remain as recorded below.

## Element query snapshots and indexed DOM loops, 2026-09-20 UTC

**c5bcd3de** adds `Element.querySelectorAll(String)` through public
`style::engine::select(..., first_only=false)`. Generated code uses a local
`std::vector<ctbrowser::element_ref>`; the document owns the nodes. The existing
Style core supplies ordering, deduplication, root/scope/shadow behavior and
detached-subtree queries. Selected elements retain the input's live Style engine.
No browser code or runtime-oracle semantics changed, and native binaries link
DOM/Core/Style without Script/AOT.

Exact zero/+1 indices guarded by the same snapshot's length now work in both
structured loop forms, including ordinary `for` bodies in the after region.
Attribute/class mutations preserve saved membership. Contracts with dataset
parameters retain the conservative backedge alias refusal. Snapshot writes,
unsafe indices, escaping handles/callbacks and unproved methods still refuse.
Raw-IR tests cover permuted state tuples, false/before edges, bad latches and
every insufficient work budget; partial proofs publish no index/element evidence.

Measured on the devbox: **16 native executions / 44 refusals**, across both
providers, optimization policies, printing layouts and GCC/Clang. Final
`native-dom-query-all.test` passed **1/1** (39.33s). Initial runtime/host CTests
passed **2/2** (0.53s); after the loop fix, host CTest passed **1/1** (0.59s).
Existing `native-dom-query.test` and `native-dom-dataset.test` passed; dataset
was repeated after the loop change. All **16** final code/test hashes match.

Exact focused commands, under `/tmp/ctbrowser-devbox-build.lock` with SSH/helper
stdin redirected from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-native-runtime ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^(ctcompile_host_contract|ctcompile_native_runtime)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(query-all|query|dataset)[.]test$'
# After the loop proof fix, local helper then devbox checks:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(query-all|dataset)[.]test$'
# Test-fixture correction: same three-target helper, then query-all alone;
# final correction used these commands:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-query-all[.]test$'
```

The first source gate exposed the missing ordinary-loop proof, now fixed.
Another control incorrectly expected an immediate local helper to refuse;
it now tests an escaping closure. Adding that helper to the branch-local
positive snapshot hit the existing local-identity boundary, so the positive
retains its direct length return. No helper proof was widened. Required
`tools/format.sh --check` retains **20** baseline diagnostics in six untouched
files; changed C++/Python, syntax and whitespace checks pass. Full CTest,
compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were skipped.

**Next exact browser boundary:** Bootstrap `bootstrap.bundle.js:341–342` uses
`Element.prototype.querySelectorAll.call(e, t)` and `querySelector.call(e, t)`.
Prove the original Element/prototype method and Function.call identities for
an explicit element and String, then reuse the existing selector helpers.
Its default `document.documentElement` receiver and NodeList spread/concat
need separate contracts; String-array iteration does not prove NodeList
iteration. Event callbacks, parent traversal and sanitizer-owned DOMParser
documents remain further boundaries. Original B/Data+B helper/inherited
constructor publication, nested Map lifetimes, full Bootstrap initialization
and the native application driver are unfinished. No push.

## LLVM command lines and lookup tables, 2026-09-20 UTC

**a21ee995** moves `ctcompile` to a generated `llvm::opt::GenericOptTable` and
`ctbaseline`/`ctpageload` to `llvm::cl::opt/list`. `ctjs-opt` and `ctjs-translate`
already use LLVM parsing through their MLIR drivers. Boost.ProgramOptions is
removed. LLVM **23** is now required even with `CTCOMPILE_ENABLE_MLIR=OFF`;
MLIR remains optional. Named options, defaults, short aliases and parse-error
exit 2 remain; help uses LLVM formatting. `ctpageload` now rejects extra inputs.

**58a56367** maps String method names to the existing native/host enums and uses
`DenseMap` for method and dataset-value proof lookup. RegExp identity, first-unit
lowercasing, receiver checks and bounded key hashing remain. **a6fe76a6** uses
`StringSet` for shape-instantiation counts. The container review kept ordered
JavaScript Map snapshots, sorted emitted fields and escape-lattice vectors;
those orders have consumers. No speedup was measured or claimed.

Measured on the devbox: selected CTests **6/6** (0.85s), selected lit **5/5**
(168.91s), final expanded CLI CTest **1/1** (0.25s). CLI checks cover aliases,
missing/duplicate/unknown arguments, empty entry, a path starting with `--`,
real bundles/manifests, baseline input order and page images. Packaging roundtrip
also checks launcher execution, output-write failures and matching rendering.
A separate MLIR-disabled configuration succeeded; it was a configure check,
not another build. All **17** changed code/test hashes match the devbox.

Commands ran under `/tmp/ctbrowser-devbox-build.lock` (SSH/helper calls used
`</dev/null` so they could not consume the enclosing shell heredoc):

```sh
tools/remote-build.sh ctcompile-tool ctcompile-tool-ctbaseline ctcompile-tool-ctpageload ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-native-reference ctcompile-test-app_bundle ctrun ctbrowse
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^(ctcompile_version|ctcompile_usage|ctcompile_help|ctcompile_rejects_nonsense|ctcompile_cli|ctcompile_host_contract)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (Packaging/roundtrip[.]test|CTNative/Browser/native-dom-(strings|dataset|class-list)[.]test|CTNative/Lowering/Objects/one-shape-one-definition[.]mlir)$'
# After the final CLI edge cases and unconditional LLVM version check:
# Local helper, then devbox CTest/configure:
tools/remote-build.sh ctcompile-tool ctcompile-tool-ctbaseline ctcompile-tool-ctpageload
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_cli$'
cmake -S ctbrowser -B build-cli-no-mlir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$PWD/tools/clang-std-embed/bin/clang++" -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew -DCTBROWSER_ENABLE_PROJECTS=ctcompile -DCTCOMPILE_ENABLE_MLIR=OFF -DCMAKE_DISABLE_FIND_PACKAGE_MLIR=ON -DBUILD_TESTING=OFF -DCTBROWSER_BUILD_EXAMPLES=OFF
```

Required `tools/format.sh --check` still reports the same **20** baseline
diagnostics in six untouched files; changed C++ and Python pass scoped
clang-format/Black checks, Python syntax and `git diff --check`. Full CTest,
full compiler lit, broad corpus/matrix, WPT/test262 and sanitizer runs were
skipped. No browser/runtime-oracle edits or push.

**Next integration boundary:** typed `Element.querySelectorAll(String)` snapshots
and proved iteration through public `style::engine::select(..., first_only=false)`.
Document-root queries need an explicit document receiver contract. Original
B/Data+B helper/inherited constructor publication, nested Map lifetimes,
retained callbacks and Shell/rendering remain. Full Bootstrap initialization
and the native application driver are unfinished.

## Native class-list integration, 2026-09-20 UTC

**6ea177bc** lifts `classList.contains/add/remove` into the public DOM token-list
API; Shell now converts arguments and delegates to that core. **7743e77a** binds
the methods in native output, including variadic/zero-argument mutations, saved
local aliases, guarded selector receivers and undefined-to-void results. Element
and class-list names use enum maps, with bounded key lengths before hashing.
Generated clients retain ordinary document ownership and link no Script/AOT.

Measured: **32 native executions / 30 refusal controls**, plus two successful
read-only dataset lowerings. Browser/host CTests passed **4/4** (0.62s); the final
host repeat passed **1/1** (0.49s) and class-list lit **1/1** (57.07s). Existing
DOM/closest/query lit cases also passed. The initial class-list run caught an
unsupported comparison in its new test control; the corrected control returns
the supported length directly. All **12** final code/test hashes match devbox.
Required formatting still reports 20 baseline diagnostics in six unchanged
files; changed files pass. Full suites, WPT/test262, broad corpus/matrix replays
and sanitizer runs were skipped. Historical measurements remain historical.

**Next browser boundary:** `Element.querySelectorAll(String)` needs a typed
snapshot of document-owned node handles and proved iteration. Reuse public
`style::engine::select(..., first_only=false)`; document-root queries need an
explicit document receiver contract. Retained callbacks, Shell/rendering,
original B/Data+B constructor publication through helpers/inheritance and nested
Map lifetimes remain. Full Bootstrap initialization and the application driver
are unfinished. No push.

[Exact changes, commands and next boundary](handoff/2026-09-20-class-list.md).

## Native browser queries and guarded handles, 2026-09-20 UTC

**fbb510e2** permits existing DOM/Style calls on a `closest()` result inside
its proved present branch. **67a8af42** adds `Element.querySelector(String)`
through the public ctbrowser selector engine and reuses that guarded borrow.
Selector chains retain their original live Style engine; document ownership,
source effect order and no-Script/VM/GC linkage remain intact. **c188f3e5**
corrects the guide's stale claim that document-owned Data sessions were only
analysis: that integration was already implemented.

The two new focused tests pass **32 native executions / 40 refusal controls**
across GCC/Clang, both optimization policies and printing layouts, including
owned-session calls. Final host CTest **1/1** (0.49s), closest/query lit **2/2**
(39.44s); the preceding DOM/DOM-session/closest selection passed **3/3**
(251.44s). The existing session test includes generated-client ASan/UBSan.
All seven final query file hashes match. Required formatting still reports
20 baseline diagnostics in six unchanged files; changed files pass.

**Next browser boundary:** `classList.contains/add/remove` still need a shared
plain C++ token-list API extracted from Shell before native binding; do not
duplicate their algorithms. Document-root queries, `querySelectorAll` iteration,
retained callbacks and Shell/rendering integration also remain. Original
B/Data+B constructor publication through helpers/inheritance and nested Maps
is unchanged from the previous handoff. Full Bootstrap initialization and the
application driver remain unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-guarded-closest.md).

## Terminal constructor publication and AND input congruence, 2026-09-20 UTC

Terminal constructor registration now runs natively. **05dbf77e** moves one
proved final `Map.set(literal, this)` immediately after each exact local `new`,
then reuses the completed-record ownership and alias proofs. The two unchanged
publication cases and four new positives add **48 native executions**, including
overwrite, deletion and saved-alias disposal. **7497424c** preserves input
congruence through AND array indices.

Focused selection: **33 observations / 128 main native executions / 66
unprepared refusals / 58 preparation refusals**. Exact arrays and host CTests
pass **1/1** each; lowering lit **3/3** and escape lit **4/4** pass. All **44**
record-pointer artifacts, **10** raw refusal controls and **ten** tested file
hashes pass. AND recording: **165 sites / 35 sound / 35 of 41 precision**, zero
violations, partial, pending or unclaimed sites. Required formatting retains
20 baseline diagnostics in six unchanged files; changed files pass. Original
class fixtures 01–29 and all 43 prior AND source functions remain unchanged.

**Next:** original B/Data+B still refuse `e.set(..., this)` through helpers and
inherited construction. Prove partial initialization, exception/reentry and
owner lifetime across the captured outer element Map and nested DATA_KEY Map,
including conditional conflict checks, nullable gets and deletion. Preserve
`e.set`, `e.remove`, `P.off`, configuration and disposal. Nonterminal publication,
transported/nested and region-local record Maps remain. Full Bootstrap and the
application driver are unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-terminal-publication.md).

## Saved record Map snapshots and OR/XOR index congruence, 2026-09-20 UTC

Saved Map aliases now participate in the fixed own-field snapshot proof.
**c4087c5c** includes their snapshots and writes before folding, enabling method
snapshots, inherited snapshots and disposal through completed record aliases.
Five new positives add **40 native executions**. Field additions, deletion,
escaping receivers and constructor publication remain refused.
**8dd68d45** preserves the transformed low-bit residue of OR/XOR array indices.

Focused selection: **60 observations / 224 main native executions / 120
unprepared refusals / 85 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3**, escape lit **4/4** pass. All **80**
record-pointer artifacts, **10** raw refusal controls and **nine** tested file
hashes pass. OR/XOR recording: **120 sites / 27 sound / 27 of 34 precision**,
zero violations, partial, pending or unclaimed sites. Required formatting
retains 20 baseline diagnostics in six unchanged files; changed files pass.
Original class fixtures 01–28 and all 28 prior OR/XOR sources remain unchanged.

**Next:** original B/Data+B still refuse constructor-time `e.set(..., this)`.
Connect partial initialization, exception/reentry and enclosing owner lifetime
proofs through the captured outer element Map, nested DATA_KEY Map and helper
calls. Preserve conflict checks, `e.remove`, `P.off`, configuration and disposal.
Transported/nested and region-local record Maps remain. Full Bootstrap and the
application driver are unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-snapshots.md).

## Saved record Map methods and AND index gaps, 2026-09-20 UTC

Saved Map aliases now call immutable instance methods in native output.
**6fe314c8** connects the existing exact record origins to the method census,
retains receiver identity across overwrite/delete, and reuses final closed-shape
receiver admission. The unchanged original alias-method case and five new
positives add **48 native executions**. **8b9d3f8b** preserves AND-mask trailing
zero bits as an index stride, admitting disjoint reloads inside those gaps.

Focused selection: **46 observations / 160 main native executions / 92
unprepared refusals / 56 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3** and escape lit **3/3** pass. Ten raw
forged-proof refusals and **60** concrete record-pointer artifact checks pass;
all ten tested file hashes match. AND oracle: **129 sites / 26 sound / 26 of 32
confined precision**, zero violations, partial, pending or unclaimed sites.
Required formatting retains 20 diagnostics in six unchanged files; changed
files pass. Original class files 01–27 and all 32 prior AND source bodies remain
unchanged.

**Next:** original B/Data+B still refuse constructor-time registration of
`this`. Prove partial publication, exception/reentry behavior and enclosing owner
lifetime through the original nested Data Maps and helper calls. Saved-alias
methods now work after completed construction; own-key snapshots and transported
record Maps still need their separate proofs. Preserve conflict checks,
`e.set`, `e.remove`, `P.off`, configuration and disposal bodies. Full Bootstrap
and the application driver remain unfinished. No browser/runtime-oracle edits
or push; full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-methods.md).

## Native record Map borrows and remainder congruence, 2026-09-20 UTC

Completed class records now run through local native Maps. **c955af5a**
connects the live retention proof to closure lifting, closed-shape groups,
exact saved-read origins and borrowed pointer emission. The six unchanged
preparation-only cases and three new positives add **72 native executions**.
Maps use the existing `map_storage<std::string, concrete_record *>`; records
stay in the enclosing stack frame and saved aliases survive overwrite/delete.
Existing object-identity Maps retain their separate representation.
**35ceaf8b** preserves remainder congruence across quotient wraps using `gcd`.
**2f10fa93** preserves branches in two older Map refusal tests.

Focused selection: **37 observations / 112 main native executions / 74
unprepared refusals / 47 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3**, escape lit **3/3** and adjacent Map lit
**6/6** pass. Eight raw refusal controls reject forged proof markers; all 36
record C++ artifacts carry concrete borrowed pointers. All 19 tested hashes
match. Remainder oracle: **183 sites / 36 sound / 36 of 41 confined precision**,
zero violations, partial, pending or unclaimed sites. Required formatting still
reports 20 diagnostics in six unchanged files; changed files pass.

**Next:** original B/Data+B still refuse constructor-time registration of
`this`. Prove partial publication, exception/reentry behavior and owner lifetime
before transporting record payloads through the original nested Data Maps and
helper calls. Current record Maps require direct entry-block operations,
literal String keys and present gets. Saved-alias methods and own-key snapshots
remain separate proofs. Preserve conflict checks, `e.set`, `e.remove`, `P.off`,
configuration and disposal bodies. Full Bootstrap and the application driver
remain unfinished. No browser/runtime-oracle edits or push; full suites and
broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-borrows.md).

## Completed record Map preparation and remainder bands, 2026-09-20 UTC

**77e02a65** prepares completed same-constructor records stored in direct local
Maps. Six new sources preserve registration, saved reads, overwrite/delete,
clear and alias writes. **They remain preparation-only: zero new native
executions.** The constructor lifter still refuses retained record shapes.
Source proofs require complete standard Map identity, entry-block owners,
literal String keys and present reads; saved constructor/method selectors and
own-key snapshots without Map alias evidence remain refused.
**5e69b712** preserves exact remainder endpoints and stride within one quotient
band, retaining wrap fallback, complete reload checks and exact replay.

Focused selection: **31 observations / 40 existing native executions / 62
unprepared refusals / 41 preparation refusals / 12 prepared native refusals**.
Exact host and arrays CTests each pass **1/1**; lowering lit **3/3** and escape
lit **3/3** pass. Remainder oracle: **141 sites / 27 sound / 27 of 32 confined
precision**, zero violations, partial, pending or unclaimed sites. All nine
tested hashes match. Required formatting retains 20 diagnostics in six unchanged
files; changed files pass. Original class sources 01–25 remain unchanged.

**Next:** connect the retained-record proof to closure lifting, Map payload
family/origin evidence, shape inference and typed pointer emission. Preserve
saved record identities across overwrite/delete and prove enclosing owner
lifetimes. Own-key snapshots need those alias edges before folding. Original
B/Data+B still refuse constructor registration; partial publication requires
exception/reentry proof before nested Data storage. Preserve conflict checks,
`e.set`, `e.remove`, `P.off` and configuration/disposal bodies. Full Bootstrap
and the application driver remain unfinished. No browser/runtime-oracle edits
or push; full suites, whole class lit and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-preparation.md).

## Inherited getters and signed remainder bounds, 2026-09-20 UTC

**ddf01d98** preserves exact inherited static getter environments, including
transitive dependencies, constructor reads, shared methods and direct/static
reads. Four unchanged sources and seven new positives add **88 native
executions**. Getter overrides and changed dependencies remain refused.
**d121e11b** proves bounded signed remainder indices while retaining exact
replay, valid final indices and complete reload checks.

Two disjoint focused class selections total **40 observations / 144 main native
executions / 80 unprepared and 49 preparation refusals**. Exact host and arrays
CTests each pass **1/1**; lowering lit **3/3** and escape lit **3/3** pass.
Remainder oracle: **99 sites / 18 sound / 18 of 23 confined precision**, zero
violations, partial, pending or unclaimed sites. All eleven tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed
files pass. Original class source files 01–24 remain unchanged.

**Next:** original B/Data+B still refuse receiver observation at registration.
First prove typed record retention after completed construction in a local Map,
including saved reads across overwrite/delete and enclosing owner lifetime.
Existing Map runtime templates can carry record pointers; source/closure proofs,
Map family/origin evidence and shape inference still need to connect. Constructor
publication needs a separate exception/reentry proof before the original nested
Data Maps can retain `this`. Preserve conflict checks, `e.set`, `e.remove`,
`P.off` and configuration/disposal bodies. Full Bootstrap and the application
driver remain unfinished. No browser/runtime-oracle edits or push. Full suites,
whole class lit, DOM replay and broad matrices were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-inherited-getters.md).

## Optional scalar Map keys and remainder indices, 2026-09-20 UTC

**7b456d79** preserves optional Number/Boolean/Null/Undefined Map keys with the
existing finite scalar carrier, SameValueZero and canonical numeric zero.
Five unchanged numeric-key sources and six new cases add **88 native executions**;
ordered numeric value snapshots add **eight**. Optional-key snapshots and mixed
String/Number or object/absent class keys remain refused. **4a0e7910** proves
bounded nonnegative Number remainder indices with exact replay and complete
reload checks. **f428fd97** retains conditional branches in three existing Map
refusal RUN lines; source bodies and expected diagnostics are unchanged.

Focused class selection: **27 observations / 128 main native executions / 54
unprepared and 37 preparation refusals**, plus **four native boundary controls**.
Exact runtime, host and arrays CTests each pass **1/1**; escape lit **3/3**, new
Map lit **1/1** and adjacent Map lit **2/2** pass. Remainder oracle: **63 sites /
nine sound / nine of 15 confined precision**, zero violations, partial, pending
or unclaimed sites. All 13 tested native/escape hashes match. Required formatting
retains 20 diagnostics in six unchanged files; changed files pass.

**Next:** original B/Data+B still refuse receiver observation at registration.
Prove typed class payloads and enclosing owner lifetime across complete
constructor/helper/Map calls, saved aliases, overwrite/delete, constructor
failure and disposal. Preserve nested Maps, conflict checks, `e.set`, `e.remove`,
`P.off` and configuration bodies. Scalar identity storage cannot substitute for
a concrete class record. Full Bootstrap and the application driver remain
unfinished. No browser/runtime-oracle edits or push. Full suites, whole class
lit, DOM replay and broad matrices were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-scalar-map-keys.md).

## Helper Map environments and signed AND masks, 2026-09-20 UTC

**a95574e6** passes exact Map environments through local captured helpers and
Data-style holders. Original helper bodies receive ordinary Map parameters;
constructor/method closures retain the corresponding immutable cells. Entry,
transitive, shared, distinct and inherited calls add **72 native executions**,
including the two original inherited-helper/holder sources. Missing user arguments
are padded before environment arguments. Complete caller, symbol, identity,
initialization, static-capture and work-budget checks remain mandatory.
**ed61b17a** extends AND index enclosures to signed/high-bit masks within a proved
conversion band, retaining exact replay and the complete reload census.

Focused class selection: **61 observations / 208 main native executions / 122
unprepared and 90 preparation refusals**, plus **ten native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
AND oracle: **96 sites / 18 sound / 18 of 24 confined precision**, zero violations,
partial, pending or unclaimed sites. All eleven final tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** original B and Data+B both refuse receiver observation at registration.
Local helper Map transport is connected; retaining `this` in the nested Data Maps
still requires typed payload and lifetime proof across construction/disposal,
set/get/remove, saved aliases, overwrite/delete and failure. Retain original
`e.set`, `e.remove`, `P.off`, nested Map creation and all configuration bodies.
Native output uses stack class records, direct functions and existing scalar Map
owners; no stored-class graph or Script/VM/GC dependency was introduced. A new
same-named helper/method control pins Node 8 versus a VM recursion throw.
Full Bootstrap and the application driver remain unfinished. No browser/runtime
changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-helper-map-environments.md).

## Inherited Map captures and signed OR/XOR bands, 2026-09-20 UTC

**d4c4a82b** preserves exact Map capture cells through inherited constructor
copies. Base and leaf slot numbers are remapped by Map identity; shared Maps,
distinct Maps, repeated ancestry, sibling leaves, ordinary inherited methods and
mixed helper captures add **48 native executions**. Original numeric-key
`class-map-inherited` now prepares and retains its optional-key carrier refusal.
**adac2c92** proves OR/XOR index enclosures within one signed conversion band and
converted sign half, preserving complete reload checks and exact write replay.

Focused class selection: **42 observations / 136 main native executions / 84
unprepared and 66 preparation refusals**, plus **six native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **4/4** pass.
OR/XOR oracle: **84 sites / 18 sound / 18 of 24 confined precision**, with zero
violations, partial, pending or unclaimed sites. All ten tested file hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** preserve Map environments through the original local Data holder/helper
calls. Those rewrites currently discard their callee capture identity; merely
allowing the capture is insufficient. Inherited direct captures now work, while
captured lexical super-method targets remain refused. Typed stored-class ownership
across set/get/remove, saved aliases, overwrite/delete and failure remains unproved.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Output retains
stack class records and existing scalar Map owners, with no Script/VM/GC or new
stored-class graph. Full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-inherited-map-captures.md).

## Direct class Map captures and OR/XOR indices, 2026-09-20 UTC

**cfc7a229** preserves immutable Map capture cells and selected slots for direct
local constructors and instance methods. Existing closure and Map lowering
proves calls, types and ownership; shared mutations, distinct Maps, mixed capture
slot renumbering and a method loop add **40 native executions**. Original numeric
field-key cases reach the optional-key carrier boundary; constructor own-callee
getter reads, helper/holder, static and inherited Map captures remain refused.
**5b2e832a** bounds nonnegative signed-i32 OR/XOR array indices while retaining
exact replay, saved children, gap contents and the complete reload census.

Final focused class selection: **38 observations / 120 main native executions /
76 unprepared and 68 preparation refusals**, plus **nine native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
OR/XOR oracle: **42 sites / seven sound / seven of 11 confined precision**, zero
violations, partial, pending or unclaimed sites. All 13 tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** preserve Map captures through the local Data holder and inherited
constructor/method invocation graph. Original Data+B, now explicitly declaring
Map identity, still refuses that boundary. Typed stored-class ownership across
set/get/remove, saved aliases, overwrites/deletes and failure remains unproved.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Native output
currently reuses shared scalar Map owners; no stored-class or cyclic ownership
graph was admitted. Full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-class-map-captures.md).

## Receiver forwarding and masked indices, 2026-09-20 UTC

**52cc51db** composes read-only constructor-receiver borrows through exact helper
chains. Global, captured and holder chains, reordered parameters and inherited
argument effects add **40 native executions**. Every leaf read must name a field
already present at the original call. Storage, returned aliases, writes, dynamic
keys, recursive proofs and unused ambient effects remain refused.
**db4d6384** bounds Number BitAnd indices with a nonnegative signed-i32 mask.
Its conservative range protects bounds/reload checks; replay updates only actual
visited elements, preserving saved children and gaps.

Focused class probe: **55 observations / 160 main native executions / 110
unprepared and 70 preparation refusals**, plus **12 native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3**, escape lit **3/3** pass.
Mask oracle: **48 sites / eight sound / eight of 12 confined precision**, zero
violations, partial, pending or unclaimed sites. All nine tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** unchanged B still refuses receiver observation; Data+B still refuses
its shared Map capture. Preserve selected Map captures/cells through class
preparation and reuse the existing closure lifter's capture transport, with a
complete Map identity/body/invocation proof. The local Data holder does not match
the published owned-global factory seam. Typed class-receiver storage still needs
ownership across constructor/dispose, saved aliases, overwrite/delete and failure.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Variable fields,
inherited getter targets, full Bootstrap and the application driver remain.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-forwarded-receivers.md).

## Constructor receiver borrows and signed output bands, 2026-09-20 UTC

**622f1ca8** proves read-only helper arguments against fields already present at
construction. Exact global, captured and holder helpers, two receiver arguments,
inherited construction and argument order add **48 native executions**. Private
capture-free direct helpers reuse the shrinking borrowed-parameter proof after
class preparation consumes their closure. Every symbol/caller remains checked;
public helpers, module references and mixed or missing arguments refuse.
**04a622d3** proves left shifts within one signed output conversion band using
exact wide products, retaining input-band, own-index and reload checks.

Final focused class probe: **63 observations / 168 main native executions / 126
unprepared and 76 preparation refusals**, plus **11 native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
Left shift: **156 sites / 30 sound / 30 of 35 confined precision**, zero
violations, partial, pending or unclaimed sites. All eleven tested hashes match.
Changed formatting passes; the required formatter retains 20 diagnostics in six
unchanged files. Full suites, whole class lit, DOM replay and broad matrices
were skipped.

**Next:** original B still refuses receiver observation at `e.set(..., this)`;
original Data+B now identifies the captured shared Map as an unsupported helper
capture. Prove that Map's identity, complete constructor/dispose call graph and
stored class-receiver ownership across set/get/remove, saved aliases and failure.
The existing Map proof accepts entry-local scalar-field leaves and entry calls;
it has no typed class-record payload. Borrowing a helper parameter does not
permit retaining it in a Map. Preserve `e.remove`, `P.off` and all original
config/disposal bodies. Variable field presence, inherited getter targets,
full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, checks and next boundary](handoff/2026-09-20-borrowed-receivers.md).

## Construction-time getters and left-shift bands, 2026-09-20 UTC

**5b171856** lets construction-time `this.constructor` reads reach the existing
exact getter proof instead of treating the prototype backedge as an own field.
Direct reads, nested methods, getter dependencies, argument order and fresh empty
object identity add **48 native executions**. Shadowing, effects, missing fields,
early snapshots and inherited getter targets still refuse. **2c7810ed** proves
bounded left shifts within one ToInt32 conversion band, preserving output bounds
and complete reload checks.

Focused class probe: **63 observations / 168 main native executions / 126
unprepared and 82 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Left shift: **120 sites / 21 sound / 21 of 26 confined
precision**, zero violations, partial, pending or unclaimed sites. All eight
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** original Bootstrap B and Data+B now refuse `class own-key snapshot
constructor observes its receiver`. Source tracing identifies the receiver
argument in `e.set(this._element, this.constructor.DATA_KEY, this)` after
`_getConfig`. Prove that registration's complete shared Map/stored-receiver
ownership and lifetime; retain `e.remove`, `P.off` and all config/disposal bodies.
Variable field presence and inherited per-leaf getter targets remain separate
obligations. Shared method/getter reads must keep the same selected target and
transitive dependencies across receivers, or require separate body proofs.
Inherited DOM, static construction, full Bootstrap and the application driver
remain unfinished. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-construction-getters.md).

## Construction-point methods and signed left shifts, 2026-09-20 UTC

**e6cfaede** proves instance-method receiver uses against the fields already
present at each constructor call. Existing-field reads/updates, nested calls,
nearest inherited overrides and argument order add **48 native executions**.
Early snapshots, missing/new fields and receiver escape still refuse. Snapshot
provenance survives inherited `super` expansion. **306af499** proves bounded
signed left-shift intermediates, including zero crossing and `INT32_MIN`.

Final focused class probe: **58 observations / 160 main native executions / 116
unprepared and 62 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Left shift: **90 sites / 15 sound / 15 of 21 confined
precision**, zero violations, partial, pending or unclaimed sites. All ten
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** original Bootstrap B and Data+B now refuse `class construction method
requires an existing own field`. `_getConfig` reaches inherited
`this.constructor` getter reads in `_mergeConfigObj`/`_typeCheckConfig`.
Prove the actual most-derived constructor/getter identity at that construction
point; it is not an own field. Variable field presence and shared Map/stored
receiver ownership remain separate obligations. Preserve `e.set`, `e.remove`,
`P.off` and all original config/disposal bodies. Inherited DOM, static
construction, full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-construction-methods.md).

## Constructor helpers and uneven shift footprints, 2026-09-20 UTC

**e88230b1** retains receiver-independent constructor helper computations for
complete callable/source proofs before folding fixed own-field snapshots and
clearing loops. Five new positives add **40 native executions**; partial receiver
observations, unused ambient effects, replaced holders and variable fields refuse.
**1f9b600f** proves uneven right-shift index footprints with a conservative dense
range; exact replay preserves children at unwritten positions.

Final focused class probe: **33 observations / 88 main native executions / 66
unprepared and 40 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Right shift: **162 sites / 34 sound / 34 of 40 confined
precision**, zero violations, partial, pending or unclaimed sites. All eight
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** both original Bootstrap B and Data+B now refuse `class own-key snapshot
constructor observes its receiver`. Source inspection identifies `_getConfig`
before `e.set` registration. Prove the instance method's complete effects at the
actual construction point, including what fields exist then; do not substitute
its eventual shape. Variable field presence, shared Map/stored-receiver ownership
and registration remain unproved. Preserve all `e.remove`/`P.off` disposal effects.
Inherited DOM/getters, static construction, full Bootstrap and the application
driver remain. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-constructor-helpers.md).

## Own-key clearing loops and signed-shift bands, 2026-09-20 UTC

**9f367dc5** proves original own-key for-of null clearing for fixed class
fields, including inherited, conditional, empty and repeated cases. A proved
break preserves exactly one store. Six new positives and two unchanged loop
promotions add **64 native executions**. **03fcd7dd** proves signed right-shift
indices within one ToInt32 conversion band.

Focused class probe: **60 observations / 200 main native executions / 120
unprepared and 53 preparation refusals**; exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Right shift: **138 sites / 27 sound / 27 of 35 confined
precision**, zero violations, partial, pending or unclaimed sites. All fourteen
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

The Array iterator replacement control records **Node 0 / VM 11**, with native
preparation refused and runtime/source unchanged. Inspected C++ uses stack
records, borrowed pointers and direct field assignments, with no iterator
container or Script/VM symbol.

**Next:** authentic Data+B still refuses fixed constructor fields. Its
helper/config/registration operations fail the census before variable presence
is isolated. Prove their complete effects and per-instance field presence, then
connect the clearing loop while retaining `e.remove`/`P.off`. Shared Map/stored
receiver ownership, inherited DOM/getters, static construction, full Bootstrap
and the application driver remain. No browser/runtime change or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-own-field-loops.md).

## Captured local holders and unsigned-shift bands, 2026-09-20 UTC

**4d909704** proves fixed local callable-holder captures in ordinary classes,
including inherited constructors, distinct base/leaf capture identities and unused
captured-slot cleanup. Eight new sources add **64 native executions**.
**74bb2f83** proves unsigned right-shift indices within the negative ToUint32 band.

Focused constructor probe: **42 observations / 128 main native executions / 84
unprepared and 45 preparation refusals**. Exact host **1/1**, arrays **1/1**,
escape lit **3/3** and DOM lit **1/1 (291.76s)** passed. DOM retains **632
observations / eight executions / 4,922 refusals**. Right shift: **99 sites / 18
sound / 18 of 24 confined precision**, zero violations, partial, pending or
unclaimed sites. All twelve tested hashes match. Changed formatting passes;
the required formatter retains 20 diagnostics in six unchanged files. Full
suites, whole class lit and broad matrices were skipped.

**Next:** authentic Data+B now reaches `class own-key snapshot requires fixed
constructor fields`, after the earlier captured-holder gate. Prove variable
own-field presence and the original clearing loop alongside complete shared Map
and stored-receiver ownership. Data registration remains unproved; preserve
`e.remove`/`P.off` and all original bodies. Inherited getters/DOM, static
construction, config/selectors/events/Popper, broader ownership and the
application driver remain. No browser/runtime change or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-captured-holders.md).

## Post-super holder calls and left-shift indices, 2026-09-20 UTC

**e055e73e** preserves post-super calls for the existing complete receiver,
holder and source-body proofs. Five new sources add **40 native executions**;
shared/multilevel inheritance, conditional values and argument order are covered.
**4015d707** proves bounded nonwrapping left-shift array indices.

Focused constructor probe: **25 observations / 72 main native executions / 50
unprepared and 20 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** passed. DOM lit **1/1 (290.83s)** retains **632 observations /
eight executions / 4,922 refusals**. Left shift: **66 sites / ten sound / 10 of 16
confined precision**, with zero violations, partial, pending or unclaimed sites.
Changed formatting passes; the required formatter retains 20 diagnostics in six
unchanged files. Full suites, whole class lit and broad matrices were skipped.

**Next:** the new original Data+B witness refuses the captured local Data holder
(`class method capture is not its constructor or an inert sibling helper`). Prove
its shared Map ownership and stored receiver using the existing seams. The unchanged
W/B-only control remains refused at fixed constructor fields. Keep the nested
callable/receiver SCF-transport control, variable fields and original own-key for-of clearing effects. Full Data/config,
inherited DOM, static construction, selectors/events/Popper, broader ownership
and the application driver remain. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-post-super-holders.md).

## Conditional field branches and right-shift indices, 2026-09-20 UTC

**e38407aa** preserves conditional constructor branches after super and
proves own-field snapshots when every branch leaves the same ordered fields.
Original source branches and nested inherited helper captures remain checked;
seven new positives and one preserved promotion add **64 native executions**.
**4a5ef7b2** independently proves bounded right-shift indices with exact conversion
and reload guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and DOM lit **1/1**
passed. Class probes passed **36 observations / 104 main executions**,
including the 64 new executions. Whole class-lit attempts exposed stale refusal
expectations; the final OWN_FIELDS group and promoted constructor were checked
directly after correction. **No full class-lit pass is claimed this session.**
DOM remains **632 observations / eight executions / 4,922 refusals**. Right shift:
**63 sites / ten sound / 10 of 15 confined precision**, zero violations, partial,
pending or unclaimed sites. Twelve tested hashes match. Changed formatting passes;
the required formatter retains 20 diagnostics in six unchanged files. Exact
failures and corrected focused checks are recorded below. Full suites and broad
matrices were skipped; no browser/runtime changes or push.

**Next measured boundary:** unchanged Bootstrap W/B now refuses
`super initialization contains an unproved call`; source inspection identifies
B's `e.set` registration. Prove authentic Data/config/receiver-getter effects,
variable per-instance fields and the original own-key for-of clearing loop,
retaining dispose's `e.remove`/`P.off`. Static construction, inherited DOM,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, focused validation and next boundary](handoff/2026-09-20-conditional-fields.md).

## Inherited own-field snapshots and complement bands, 2026-09-20 UTC

**117783b6** proves fixed inherited own-field snapshots. Shared inherited methods
require the same ordered field set on every descendant, including empty shapes;
leaf-only snapshots may add fields after proved super initialization. All ancestor
writes and partial-construction observations remain checked. Six new positives add
**48 native executions**. **170eb7e6** independently admits bounded BitNot indices
within one ToInt32 conversion band, preserving discontinuity and reload guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and class initialization/DOM
lit **2/2 (425.99s)** passed. Class: **292 observations / 812 main executions /
584 unprepared and 311 preparation refusals**. DOM remains **632 / eight / 4,922**.
BitNot: **60 sites / 11 sound / 11 of 14 confined precision**, zero violations,
partial, pending or unclaimed sites. Eleven tested hashes match. Changed formatting
passes; the required formatter retains 20 diagnostics in six unchanged files.
Full suites and broad matrices were skipped; no browser/runtime changes or push.

**Next measured boundary:** unchanged Bootstrap W/B now refuses
`super condition is not a proved Boolean` in B's conditional constructor.
Prove conditional field presence and the original own-key for-of clearing loop,
retaining dispose's `e.remove`/`P.off` effects. Distinct inherited shapes, implicit
rest/apply, static `this.getInstance`/`new this`, inherited DOM/getters,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, validation and next boundary](handoff/2026-09-20-inherited-fields.md).

## Fixed own-field snapshots and BitNot indices, 2026-09-20 UTC

**674b985c** folds fixed own-field snapshot length/index observations to constants,
including exact indexed clearing. Five new positives add **40 native executions**.
Constructor field presence/order and all receiver writes are checked; original
method bodies and frames remain. Replacement returns and cached boxed-local
producers have dedicated regressions. **89f987bd** independently proves bounded
BitNot indices, rejecting signed-i32 wraparound and preserving reload/alias guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and class initialization/DOM
lit **2/2 (399.41s)** passed. Class: **276 observations / 764 main executions /
552 unprepared and 299 preparation refusals**. DOM remains **632 / eight / 4,922**.
BitNot oracle: **60 sites / nine sound / 9 of 14 confined precision**; the preserved
unary source now measures **63 / ten / 10 of 14**. Zero violations, partial,
pending or unclaimed sites. Twelve tested hashes match; changed formatting passes.
Required formatter retains 20 diagnostics in six unchanged files. Full suites and
broad matrices were skipped; no browser/runtime change or push.

**Next measured boundary:** original Bootstrap W/B now refuses
`class own-key snapshot requires fixed constructor fields`. The current proof
rejects inheritance; B also conditionally initializes its fields. The preserved
standalone own-key for-of clearing control still refuses snapshot consumption.
Prove inherited/conditional field presence and iterator consumption while retaining
all dispose effects. Static `this.getInstance`/`new this`, inherited DOM/getters,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, checks and next boundary](handoff/2026-09-20-own-fields.md).

## Own static methods and unary indices, 2026-09-20 UTC

**1bd7e508** proves immutable own static method slots and exact-constructor calls,
reusing existing capture/getter proofs. Four positives add **32 native executions**;
original target frames and roots survive. Complete unused bodies retain strict
source checks. Three DOM controls cover shared helpers and both declaration
orders. **6f34888c** independently proves bounded unary plus/minus array indices.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, class initialization **1/1**
and corrected DOM lit **1/1** passed. Class: **258 observations / 724 main executions /
516 unprepared and 281 preparation refusals**. DOM: **632 / eight / 4,922**.
Unary oracle: **63 sites / nine sound / 9 of 14 confined precision**, zero violations,
partial, pending or unclaimed sites. All 13 tested source hashes match. Formatter
retains 20 diagnostics in six unchanged files; changed formatting passes. The first
DOM run failed a new diagnostic assertion; its source was preserved and the
corrected case passed. Full suites and broad matrices were skipped.

**Next measured boundary:** unchanged Bootstrap W/B gets past static-slot collection
and refuses `class receiver escapes or observes a prototype/descriptor`. Source
inspection points to B.dispose's `Object.getOwnPropertyNames(this)` and `this[t]`
clearing loop. Prove own-field enumeration/clearing without allowing arbitrary
receiver escape. Full static bodies, `this.getInstance`/`new this`, inherited
per-leaf getter/DOM proof, H/config/selectors/events/Popper, broader ownership and
the application driver remain. No browser/runtime changes or push.

[Exact changes, checks and next boundary](handoff/2026-09-20-static-methods.md).

## Nested helper captures and negative divisors, 2026-09-20 UTC

**c5cd6e4f** proves bounded immutable nested helper captures using the existing
fixed-cell and complete source-body checks. Seven new executable cases add
**56 native executions**, including a helper-only root/frame preservation check.
Shared targets, inherited capture identities, argument order and refusal controls
remain covered. **353114ba** proves exact bounded negative-divisor array
index overwrites with positive stride magnitudes and full reload-overlap checks.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, and class initialization/DOM
lit **2/2 (366.33s)** passed. Class: **246 observations / 692 main native executions /
492 unprepared and 269 preparation refusals**. DOM remains **632 / eight / 4,910**.
Negative-divisor oracle: **69 sites / nine sound / 9 of 13 confined precision**,
zero violations, partial, pending or unclaimed sites. Nine source hashes match.
Formatter retains 20 diagnostics in six unchanged files; changed checks pass.
Full suites and broad matrices were skipped. No browser/runtime change or push.

**Next measured boundary:** unchanged Bootstrap W/B now passes nested `a` → `r`
captures and refuses ordinary static method setup (`getInstance`,
`getOrCreateInstance`, `eventName`). Reuse the existing method/capture/getter
proofs for fixed constructor slots and exact receivers; retain all original
bodies. Inherited DOM per-leaf receiver/getters, `new this`, selectors and full
H/config/events/Popper remain separate proof work, as do broader ownership,
own-data provenance and the application driver.

[Exact changes, checks and next boundary](handoff/2026-09-20-nested-captures.md).

## Inherited helper captures and negative-scale overwrites, 2026-09-20 UTC

**1ef56773** completes inherited constructor helper captures: each copied read
keeps its original helper identity across base/leaf slots, transitive chains and
shared bases. Four new executable cases plus one promotion add **40 native
executions**. The original object-mutating order source remains preparation-only
with its ownership refusal; a primitive companion executes with **1010013**.

**bd099c65** proves bounded negative-scaled own-index overwrites using the existing
Number product and reload-overlap proofs. Earlier **d24dd479** (sibling helper
captures) and **b85f8c14** (reversed subtraction overwrites) are also now recorded
in the detailed handoff; both had landed before this recovery.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, and class initialization/DOM
lit **2/2 (340.27s)** passed. Class: **230 observations / 636 main native executions /
460 unprepared and 254 preparation refusals**. DOM remains **632 / eight / 4,910**.
Negative-scale oracle: **66 sites / nine sound / 9 of 13 confined precision**,
zero violations, partial, pending or unclaimed sites. Eight source hashes match.
Formatter retains 20 diagnostics in six unchanged files; changed checks pass.
Full suites and broad matrices were skipped. No browser/runtime change or push.

**Next:** Bootstrap's captured `a` helper itself captures `r`. Prove bounded
immutable nested helper captures with the complete body census, then inherited
DOM per-leaf receiver/getter proof. `a` also references `n` and `document` in its
selector branch; the null-input observation grants no authority there. Full
H/config, selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, validation, recovery and next boundary](handoff/2026-09-20-helper-captures.md).

## Lexical super and composed overwrites, 2026-09-20 UTC

**6acc1c5c** resumes lexical-super dispatch: immutable nearest-base selection,
leaf receiver and argument/effect order are proved before expanding linear,
capture-free target bodies. The preserved W/B/Qi dispatch now runs natively with
**118**. Four new positives plus that promotion add **40 native executions**;
every original source section remains. Original and shadowed bodies pass the
complete census before unread inherited slots disappear. Rooted method and
constructor targets refuse before frame-stripping expansion.

**950dfb64** proves composed affine array overwrites with exact bounded signed
intermediate Numbers and positive strides. Reload overlap, growth, saved aliases,
cycles and work limits retain their refusals.

Focused arrays **1/1**, escape lit **4/4**, and class initialization/DOM lit
**2/2 (310.09s)** passed. Class: **207 observations / 564 main native executions /
414 unprepared and 234 preparation refusals**. DOM remains **632 observations /
eight executions / 4,910 refusals**. The final constructor-root guard then passed
a targeted **14-observation / 48-execution** probe and host **1/1**; the two lit
cases were not replayed after that guard. Composed oracle: **54 sites / seven
sound / 7 of 10 precision**, zero violations, partial, pending or unclaimed sites.
Ten source hashes match. Formatter retains 20 baseline diagnostics in six
unchanged files; changed checks pass. Full suites and broad matrices were skipped.

**Next:** authentic W/B helper and constructor captures, starting with W's `r`
and B's `a(t)`, then inherited DOM receiver/getter proof. The original W/B source
still reaches its captured-helper refusal; complete H/config source still reaches
an unproved closure. Configuration, selectors/events/Popper, broader ownership
and the application driver remain. No browser/Script or compliance change.

[Exact changes, commands, measurements, failures and next boundary](handoff/2026-09-20-lexical-super.md).

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
AGENT-SYNC.jsonl history is archived under the user's approval. Full suites and broad
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
