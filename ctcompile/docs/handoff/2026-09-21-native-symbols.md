# Native Symbol values, 2026-09-21 UTC

Continued clean `d2a75ef0` on `ctcompile-v1`, following the user's Symbol priority.
The previous Number/return-dispatch thread was committed. Initial Linux process
inspection (70 processes) and Windows CIM inspection (353) found no Claude
executable, Node CLI or loop. Pre-landing inspection (68 Linux, 355 Windows)
again confirmed stopped; the Core agent also checked before its edit batch.
Checks and claims were recorded through `agent-sync.py`.

Two agents audited the source proof and Core boundaries. The Core agent
completed its extraction. A service rate limit interrupted the test agent after
it wrote the fixture; the main thread reviewed, completed and gated that draft.
The later independent runtime review also hit the rate limit; the main thread
reviewed that implementation directly.

## Landed

- `36bfb67e`: `ctbrowser/core/symbol.hpp` owns the existing key/description
  payload, identity equality, description-presence rule and explicit formatting.
  `well_known_symbols.def` is the single 15-name catalog. Core CMake installs
  both. `script/value/base.hpp` retains the old Symbol constructor and field
  access through the Core base. `Script/builtins/text/symbol.cpp` and
  `Script/builtins/async.cpp` delegate creation and formatting to Core. Registry,
  key reconstruction, per-context counters, descriptors and order are unchanged.
- `498a34bc`: `Runtime/Symbol.hpp`, included by `ctnative.hpp`, provides
  `js_symbol_t` and `Symbol`. Its 15 immutable well-known properties include
  `Symbol.hasInstance`. Fresh calls accept no description, `undefined_t`, or
  `js_string`. Descriptions return owning `optional<js_string>` snapshots;
  `toString()` and `valueOf()` also have `Symbol.prototype.*.call(symbol)` forms.
  Copy and move preserve primitive identity; implicit numeric/String conversion
  and calling a Symbol key are unavailable.

Well-known values use constexpr enum identities without allocating their Core
payloads at startup. Fresh values own the Core payload by value, with one atomic
serial source across translation units and factory copies. Exhaustion throws
before serial reuse. There is no GC, Script context, reference-counted graph or
generic property dictionary. Native registry operations and source Symbol
lowering are not implemented in this slice.

## Focused validation

All builds and executable checks ran on the devbox under the shared build lock:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime \
  ctcompile-test-native-reference ctbrowser-test-symbol_basics \
  ctbrowser-test-property_attributes ctbrowser-test-script_gc </dev/null
tools/remote-build.sh ctcompile-test-native-runtime </dev/null
```

The initial **218-step build** and final focused incremental build passed.
Exact CTest selection `^(symbol_basics|property_attributes|ctcompile_native_runtime)$`
passed **3/3 in 0.07 seconds**. The final move-preservation change then passed
`^ctcompile_native_runtime$` **1/1 in 0.02 seconds**.
`build/unittests/ctbrowser-test-script_gc symbol` passed its three registry
identity/lifetime checks; the other GC groups were not selected.

Three distinct selected lit cases passed:

- `CTNative/Lowering/Scalars/symbol-boundary.test` and
  `Target/Cpp/native-number.mlir`: **2/2 in 4.43 seconds**.
- `CTNative/Lowering/Admission/refusal-default-arm.mlir`: **1/1 in 0.06 seconds**.
- After preserving identity in moved-from values, the Symbol fixture passed
  again **1/1 in 4.16 seconds**.

The new fixture checks **25 primitive observations** against Node and the VM,
with native API executables built under GCC and Clang. It covers all 15 fixed
keys, same-description freshness, cross-translation-unit/static-initialization
identity, factory copies, copy/move assignment, standalone-header inclusion,
absent/empty/NUL/surrogate descriptions, owned snapshots and prototype methods.
The existing compilation-unit harness audits native symbols against its
VM-linked positive control, checks transcripts and rejects the mutated alias
result. Three source programs retain explicit refusals. These are handwritten
native API clients, not newly emitted Symbol programs.

All **nine final code/test SHA-256 hashes match the devbox**. Six changed C++
files pass scoped pinned formatting, the embedded C++ was formatted, and
whitespace passes. Required `tools/format.sh --check` retains **16 existing
diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`HostContract/ProviderPaths.h` (2), `PartialEvaluation/Heap.h` (4), and
`Symbolic/Facts.cpp` (8). Its early C++ failure skips broader Python/web stages.

No full CTest/lit, broad corpus/matrix, WPT/test262, sanitizer, full wtfjs or full
Bootstrap replay ran. No local build or push occurred.
Logs: `/tmp/ctcompile-symbol-{build,gate,final-gate,oracle,format,hashes}.log`.
Final manifest: `/tmp/ctcompile-symbol-sha256.txt`.

## Measured hook boundary and next step

`Symbol.hasInstance` is a Symbol key. The callable is the constructor's
`constructor[Symbol.hasInstance]`. A probe with a non-callable object holding
that hook, returning a truthy String for `7`, reports `calls=1,result=true` in
Node and `calls=0,result=false` in the VM. The latter calls OrdinaryHasInstance
directly. The probe is `/tmp/ctcompile-symbol-hook-gap.js`; its source and
results are retained in `plans/native-js-semantics.md`. Existing VM implicit
Symbol-to-String conversion gaps also remain; native conversion is explicit.

Next extend the fingerprint-bound standard-intrinsic contract to prove direct
well-known Symbol reads, census every global use and refuse mutation/escape.
Add a distinct source Symbol carrier for equality, truthiness and `typeof`.
Do not trust the spelling `Symbol` or `hasInstance` alone. Hook lookup/call,
Boolean conversion of hook results, invalid RHS errors and default prototype
matching require separate proofs and oracle work. Only an equivalent proved
default path may use `std::holds_alternative<T>` in the planned wrapper.

`Symbol.for`/`keyFor`, symbol-keyed generated fields, the retained sibling-capture
boundary, primitive/String ordering, collections/document views, full Bootstrap
and the application driver remain unfinished.
