# Native JavaScript Boolean carrier, 2026-09-20 UTC

**92024490** continued the primitive migration from clean **30a74866**, following
[native-js-types.md](../plans/native-js-types.md). The runtime and test work was
delegated in parallel; root completed the drafts after both workers hit service
limits. A separate read-only review found no blocker in the runtime, lowering,
Map, closure, shape or public browser boundaries.

`ctnative::js_boolean_t` stores one Boolean and accepts only an explicit `bool`
construction. Contextual conversion to C++ `bool` is explicit; `.to_number()`
and the existing free conversion boundary produce 0 or 1 without implicit
numeric promotion. Semantic Boolean literals, comparisons, calls/returns,
captured fields, method signatures, Map keys/payloads and global observations
now use the class. Tagged optional carriers retain their existing layouts.

MLIR i1 and raw C++ `bool` remain internal conditions. Public JSON storage,
DOM force flags and core predicate results use explicit adapters. Browser
behavior still calls ctbrowser's public subsystems; no Script dependency,
source-admission expansion, owner-model change or VM modification was added.
Boolean Array admission remains unchanged.

The mixed-Map gate exposed two historical key refusals that **7b456d79** had
already admitted. Their unchanged Boolean/Number/null source bodies now check
complete native admission and the tagged-key factory, including forged facts
and repeated lowering. The other 38 refusal fixtures remain intact. This is
an expectation repair for existing support, not newly admitted source.

Likewise, the JSON `Config` key fixture was already supported by **84b3357a**.
It now runs through the existing positive machinery with its earlier DOM read
preserved, checking `has:good|get:data-bs--config` against Node and the VM.

The user's requested typed `instanceof` wrapper is scheduled with class/prototype
work in the maintained design. It calls a proved `Symbol.hasInstance` hook with
the correct receiver and Boolean conversion, or uses `std::holds_alternative`
when constructor/prototype proofs permit that default test. Lookup, effects,
exceptions and inheritance stay observable. Unknown hooks remain diagnostics;
general `instanceof` lowering was not added in this Boolean migration.

## Validation

The affected devbox build passed; runtime CTest passed **1/1** (0.02s total).
All **17 distinct selected lit cases** and **11 selected ownership cases** passed
across the focused runs and corrections below. All **35** final code/test
SHA-256 hashes matched the devbox before commit. This is focused coverage, not
a full-suite pass.

Build and test commands ran under `/tmp/ctbrowser-devbox-build.lock`:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions ctcompile-native-pipeline-closures ctcompile-native-pipeline-returned_closures ctcompile-native-pipeline-maps
# On the devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
```

Subsequent syncs used the explicit `ctjs-opt ctjs-translate` targets. Lit ran
with `~/.lit-venv/bin/lit -v build/ctcompile/test` and exact path filters.
The complete set of selected paths, each eventually passing:

```text
CTNative/Browser/native-dom.test
CTNative/Browser/native-dom-strings.test
CTNative/Browser/native-dom-json.test
CTNative/Fixtures/Scalars/optional-scalars.test
CTNative/Fixtures/Scalars/scalar-unions.test
CTNative/Fixtures/Closures/closure.test
CTNative/Fixtures/Closures/returned-closure.test
CTNative/Fixtures/Maps/map.test
CTNative/Lowering/Scalars/optional-scalars.mlir
CTNative/Lowering/Scalars/scalar-unions.mlir
CTNative/Lowering/Maps/maps.mlir
CTNative/Lowering/Maps/map-representation.mlir
CTNative/Lowering/Maps/map-mixed.mlir
CTNative/Lowering/Maps/nullable-scalar-keys.mlir
CTNative/Lowering/Maps/dom-map-keys.test
CTNative/Lowering/Objects/one-shape-one-definition.mlir
CTNative/Ownership/global-methods.test
```

The first eight-case run passed **7/8** (11.26s); the failure was a raw Map
return-type pin. The next five-case run passed **2/5** (9.86s), exposing the
explicit cast spelling and raw-bool browser clients. The corrected five-case
run passed **3/5** (259.91s), with remaining String observer and stale Map key
refusal expectations. The six-case run then passed **5/6** (163.83s), leaving
the JSON observer's conversion into public `json_value`. Its first rerun passed
all positive execution/lifetime checks but hit the stale uppercase-key refusal
(80.16s). The final exact JSON rerun passed **1/1** (88.93s):

```sh
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-json[.]test$'
```

Ownership checks called the existing `driver_execution.setup()` and
`driver_execution.check_positive(...)` from
`CTNative.Ownership.native_owned_global_maps`, with `PYTHONPATH=ctcompile/test`,
`--translate build/ctcompile/tools/ctjs-translate/ctjs-translate`,
`--opt build/ctcompile/tools/ctjs-opt/ctjs-opt`,
`--node /home/ubuntu/tools/node-v26.8.1/bin/node`,
`--reference build/ctcompile/test/ctcompile-test-native-reference`, and
`--work /tmp/ctcompile-boolean-ownership`. Exactly these cases were selected:

```text
boolean_result
shared_parameter_bool
result_bool
result_seeded_bool
result_seeded_bool_string_contents
nullable_payload_mixed_readback
saved_join_string_saved
nullable_nested_result_saved
local_field_boolean
constant_boolean_branch_lifetime
zero_size_both_clear_false
```

Five passed before a shared C++ observer's raw Boolean arguments failed; four
more passed after that fix. The Boolean branch lifetime execution and sanitizers
then passed, but its dataflow checker needed typed literal/cast recognition.
The final two cases passed after that repair. Previously passing cases were
not replayed. An initial ownership invocation used unavailable bare `node`;
the configured Node path above corrected that invocation. One earlier lit
invocation failed shell quoting before testing; a quoted filter corrected it.

The selected fixture, Map, String/JSON and ownership drivers compare against
their existing interpreter/Node oracles and exercise both compiler/printing
layouts, source/symbol checks and negative controls. Their existing ASan/UBSan
and saved-owner lifetime checks ran. DOM action/key checks use public platform
APIs rather than a full VM/browser differential. No broad sanitizer matrix ran.

Changed C++ formatting, Python Black/syntax and whitespace checks pass. Required
`tools/format.sh --check` reports the same **16 pre-existing diagnostics** in
four untouched files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h`, and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Full CTest/lit, broad corpus/matrix
and WPT/test262 were skipped under the focused policy. No push.

Process inspection found no Claude identity among 65 Linux and 356 Windows
processes, but 56 Linux executable paths were unreadable. Availability was
treated as uncertain; edits stayed in ctcompile and the requested external plan.

## Next boundary

Migrate Number and String carriers in coherent batches. The current global
`using js_num = double` must become the qualified `ctnative::js_num<double>`
without breaking literals, arithmetic/conversion helpers, signatures, optional
joins, Map keys, captures, printing or deduced-type checks. Preserve NaN,
infinities, signed zero and JavaScript arithmetic where it differs from C++.
Do not broaden proofs merely because a class exposes an operation.

The browser boundary remains NodeList indexed slots above 1,000,000: Shell
returns undefined there, while direct native indexing still returns an element.
Resolve that before expanding indexed Bootstrap `R.find` consumers. Keep the
independent 2^24 spread cap and current count-only integration. Document roots,
Object/Array prototypes, collection/document views, BigInt/Symbol, full Bootstrap
and the application driver remain unfinished.
