# Native JavaScript exceptions using C++ unwinding

**Status: first numeric catch implementation.** Native lowering recovers one
acyclic importer handler and emits real C++ `throw` and typed `catch`, with
numeric payloads and numeric/boolean catch state. Recovery is transactional;
type/effect admission must prove every other protected operation cannot throw a
JavaScript value. General throwing callees, nested handlers and finally
completions remain further work. This is separate from
[private provider mutation summaries](native-provider-mutations.md).

`console.error(...)` is an ordinary call, not a JavaScript `throw`. Its callback
can return, throw, mutate publication, change Maps or reenter the program.
Adding C++ exception support supplies neither its effect summary nor permission
to carry private Map facts across it. The exact Bootstrap conflict path keeps
its existing provider/reentry boundary.

## Original prerequisite audit

The 2026-09-06 read-only devbox audit used the exception paths present at
`0af2b61`, during the subsequent provider/callback work. No C++ build was part
of the audit. Sources, raw/native IR and logs are under
`/tmp/ctcompile-native-exception-audit/`; `report.json` records all five cases.
Each source was imported and passed through:

```sh
ctjs-translate --ctbrowser-js-to-ctjs CASE.js -o CASE.raw.mlir
ctjs-opt CASE.raw.mlir --ctjs-resolve-globals --ctjs-lift-to-scf \
  --ctnative-lower-to-emitc=optimize=false -o CASE.native.mlir
```

| Case | Observed boundary |
|---|---|
| `leaf.js`: `function fail() { throw 42; }` | Both source functions import; `fail` refuses with `` `ctjs.throw` is not native yet ``. Its caller retains an unresolved boxed result. |
| `single_catch.js` | Both functions import; `guarded` receives the effectful `ctjs.push_handler` structuring diagnostic and native `unstructured control flow`. |
| `bare_finally.js`: try/return with finally, without catch | Both functions import; the same handler structuring boundary refuses native lowering. |
| `catch_finally.js` | Function 1 is skipped: more than one protected region in a function. |
| `nested_catch.js` | Function 1 is skipped for the same protected-region limit. |

The importer in
[BytecodeImport.cpp](../lib/CTJS/Import/BytecodeImport.cpp) explicitly refuses
more than one `push_handler` per function. It does **not** refuse every bare
finally: that form can have one handler, as the measured case demonstrates.
Its handler import also refuses a catch pad reachable without throwing.
Removing these guards alone would not implement the missing semantics.

The [CTJS exception operations](../include/ctcompile/CTJS/IR/Ops/Functions.td)
already describe `throw`, `push_handler`, `pop_handler` and `resume_throw`.
[CheckOp and CatchLandOp](../include/ctcompile/CTJS/IR/Ops/Runtime.td) preserve
the exceptional successor and thrown value. `resume_throw` has no lowering;
the current bytecode compiler's finally dispatch instead emits `throw_value`
for its saved thrown completion.

The important state is at the **throwing operation**, not at try entry.
`CheckOp` carries the pre-operation register vector into the handler; the
throwing operation's result must not be read on its failure path. An instruction
such as a method call can contain both a fallible property lookup and a
fallible invocation. The importer preserves the register state before that
instruction and inserts checks around the resulting control flow.

The boxed backend already uses `ct_aot_throw`, `ct_aot_catch_land` and the
`caught`/`unwound`/`failed` status protocol. See
[Status.cpp](../lib/CTJS/Lowering/EmitC/Status.cpp) and
[aot_bridge.cpp](../../ctbrowser/lib/Script/aot_bridge.cpp). This protocol is
not a native C++ exception implementation and must remain compatible with
interpreted callers. Generic CFG structuring rejects the effectful
`push_handler` and `check` terminators; the existing
[handler structuring regression](../test/Lowering/EmitC/lift-to-scf-handlers.mlir)
records both obstacles. Declaring them pure would allow invalid reordering.

## Recovered native regions

The initial source gate is a same-function numeric throw, with both normal and
exceptional paths and a local modified after handler installation:

```js
function guarded(flag) {
    var mark = 0;
    try {
        mark = 10;
        if (flag) { throw 32; }
        mark = 20;
    } catch (value) {
        return mark + value;
    }
    return mark;
}
var caught42 = guarded(true);
var normal20 = guarded(false);
```

The gate requires **2/2 native**, `caught42=42` and `normal20=20`, with no VM
symbols. Restoring `mark` from try entry incorrectly yields 32. The regression
executes that deliberately wrong source and checks that the result comparison
rejects it. The normal invocation checks that entering the protected region
alone does not trigger the catch.

LLVM's `transformCFGToSCF` requires successor-bearing terminators to be free of
effects. Its public transformation hooks cannot waive that requirement for
`push_handler` or `check`. A project exception-region extension lets us reuse
the installed LLVM implementation without a source fork or a false purity
annotation.

1. `ctjs-lift-to-scf` preserves primitive handler candidates before ordinary
   simplification can erase register correspondence. The syntactic filter
   excludes calls, closures, properties, allocation, cells and global effects;
   those functions keep the existing simplification needed by closure lifting.
   This filter grants no type/effect proof. Native recovery in
   [Exceptions/Recovery.cpp](../lib/CTNative/Lowering/Exceptions/Recovery.cpp)
   checks one entry handler, a dedicated landing, complete register vectors,
   active-handler balance, throw-only blocks and acyclic normal/catch tails.
   It clones the tails separately, including shared continuations. Recovery
   works on one disposable function clone under `exception-max-steps` (default
   100000). Unsupported structure or exhausted work leaves the original body.
2. [CTJS structured exception operations](../include/ctcompile/CTJS/IR/Ops/Exceptions.td)
   use one body completion, `try_exit`, carrying a boolean throw flag, normal
   result, payload and catch state. Normal and exceptional exits have identical
   operation shape, so LLVM can structure their ordinary branches together.
   The `RegionBranch` interfaces route the selected values to the catch or the
   normal result; an unselected poison value does not supply catch state.
   The catch ends with `try_yield`. Unused state arguments are removed. LLVM's
   completion-dispatch switches become nested `if` regions; unused result slots
   and explicitly all-poison result slots are pruned without removing effects.
3. Sparse type inference follows those region edges. Numeric payloads and
   numeric/boolean state are required even when the catch ignores its payload.
   [Admission.cpp](../lib/CTNative/Lowering/Exceptions/Admission.cpp) permits
   only proved nonthrowing primitive operations inside both regions. Calls,
   properties, allocation and unknown effects refuse. Recovery is rolled back
   if admission or the closed call-component check fails.
4. [Typed native operations](../include/ctcompile/CTNative/IR/CTNativeOps.td)
   retain the boundary through EmitC cleanup. `cpp_throw` is effectful and
   prints a C++ throw immediately before its IR region terminator; that
   terminator is unreachable at runtime. `cpp_try` requires single blocks
   ending in `cpp_try_end`. Its printer declares scoped, collision-safe catch
   bindings. Only the runtime type declaration uses `emitc.verbatim`.

Catch-visible variables require storage whose lifetime spans both regions.
Only live catch state gets mutable outer slots, all written before the throw.
The handler copies those slots into SSA bindings after catching the owning
numeric payload. A separate outer slot carries the normal/catch result.
Ordinary SSA temporaries retain the existing const/constexpr printing policy.
For a future throwing assignment, the pre-call value must be saved and the
assigned value published only on normal return; calls are refused for now.

The implementation is organized under `Lowering/Exceptions/` and
`Target/Cpp/Exceptions/`, keeping recovery, admission and printing separate.

A finally body that always returns can already be reduced by the bytecode
compiler to this same completion shape. Recovery can preserve that equivalent
CFG, including the overriding return. This does not implement general pending
completions, rethrows from finally or multiple handlers. Those remain below.

## Owning thrown values

The implemented carrier is `struct js_exception { js_num value; };`, caught
as `ctnative::js_exception const &`. A future expanded carrier is conceptually:

```cpp
using thrown_value = std::variant<undefined_t, null_t, bool, js_num, std::string>;
struct js_exception { thrown_value value; };
```

The variant is a proposed extension, not an existing runtime API. The
representation must keep JavaScript
tags distinct as more primitives are admitted. Numeric payloads retain their
double bits, including NaN and negative zero. Strings use the native owning
string carrier; no view into a departing frame or C++ diagnostic text substitutes
for a JavaScript string value.

Emit an owning `js_exception` and catch `js_exception const &`. Binding the
JavaScript catch variable copies its value or owning handle into independently
valid storage. A catch variable that is returned, saved or captured cannot
borrow the C++ exception object's lifetime. An explicit `throw e` evaluates
the current JavaScript value of `e`; it is not automatically a C++ bare rethrow.

JavaScript may throw any value, including objects, functions, Maps, symbols and
BigInts. Unsupported payload types must refuse this first tier. Later object
support must reuse a proved owning carrier, preserve identity across throw/catch,
and keep its fields and reachable graph alive after the throwing frame dies.
The existing pointer to a confined local object is insufficient. A copied
64-bit boxed value without VM rooting, or an untyped borrowed pointer, is not
ownership. No new exception escape exemption belongs in Map/object analysis
until its lifetime and alias tests pass.

Exception formatting is also separate. The current uncaught-JS-value path can
invoke a thrown object's `toString`; formatting an unknown thrown object is
therefore a reentry effect. Do not eagerly stringify a payload during throw or
pretend `std::exception::what()` implements that behavior.

## Finally and rethrow

C++ stack unwinding correctly destroys native RAII values. A JavaScript finally
body additionally runs on normal completion, return, break, continue and throw,
and can replace any of those completions. A destructor or scope-exit callback
does not implement this rule: a source return/loop exit must change the pending
completion, and throwing from a destructor during unwinding can terminate C++.

Keep the bytecode compiler's explicit completion design from
[statements.cpp](../../ctbrowser/lib/Script/compile/statements.cpp):

| Pending completion | State carried through finally |
|---|---|
| Normal | Normal continuation |
| Return | Owning result evaluated before finally |
| Break / continue | Exact enclosing control target |
| Throw | Owning thrown value, or an owning saved C++ exception object |

Run one logical copy of finally. If it completes normally, resume the saved
completion. If it completes abruptly, replace the saved completion with the
new one. Protect a catch body when its throws must also run finally. Preserve
nested ordering; a return value computed before finally must not be recomputed
after the finally mutates its source variable.

A pending C++ exception may be retained with `std::exception_ptr` and resumed
with `std::rethrow_exception`. Bare `throw;` only works within an active C++
handler and cannot be emitted after that handler has ended. A saved JS payload
is another valid representation if its owning identity is preserved. Whichever
is chosen, importer support for all required handlers, completion exits and
rethrows must land together. General finally and nested-handler paths remain
outside native exception admission; the equivalent single-handler override
case above does not remove these requirements.

## Runtime and foreign boundaries

Generated JS catch clauses catch only the dedicated JavaScript exception type.
`std::bad_alloc`, `std::length_error`, internal invariant failures and other
C++ failures are not automatically JavaScript thrown values. Keep an explicit
outer runtime-failure policy; do not route `catch (...)` or
`catch (std::exception const &)` into a JavaScript catch. Existing native helper
invariant failures that terminate are not newly catchable JS errors either.

Throw-payload allocation can itself fail. Preserve the runtime-failure category
rather than manufacture a JavaScript string or Error. The initial acceptance
scope does not promise recovery from allocator exhaustion or execution of JS
finally following an engine failure; that policy needs a separate contract.

The first source tier catches throws in the same function. Future propagation
must stay within the admitted native C++ call component.
Unknown C++ callbacks, external libraries, C ABI entry points and mixed
interpreter/native dispatch need explicit adapters. A future adapter must catch
the native JS exception before returning through the foreign ABI, install a
properly rooted runtime thrown value, and produce the existing status protocol.
Calls from native code into the interpreter need the reverse conversion.
Rethrow must preserve identity, and cleanup must occur exactly once on both
sides. No unreviewed C++ exception should escape a `noexcept` or foreign frame.

Exception edges invalidate normal-return-only provider facts. A successfully
summarized Map mutation describes the path after normal return; it says nothing
about a catch continuation after allocation, mutation or callback failure.
Keep that distinction even after C++ exceptions are available.

## Measured validation, 2026-09-07

[The source regression](../test/CTNative/Lowering/native-exceptions.mlir) admits
all seven positive programs at **2/2 native each**. Their fourteen observations
match Node and the running interpreter in explicit/deduced C++ under GCC 13 and
Clang 18, with no VM symbols in source or binaries. The original guarded case
also passes with default native optimizations enabled.

| Case | Observations |
|---|---|
| One catch with modified local | caught 42, normal 20 |
| Two throw sites | 42, 107, normal 20 |
| Shared catch/normal continuation | caught 14, normal 21 |
| Return inside try | caught 10, normal 7 |
| Unconditional throw and catch-variable reassignment | 33 |
| Boolean live catch state | caught 42, normal 0 |
| Already lowered unconditional finally override | 7 on both paths |

Eleven controls remain refused, retaining handler/check/throw counts and source
function denominators. Object, boolean, string, null, undefined and mixed
payloads, implicit property errors, throwing callees, pending finally and
nested handlers stay outside this tier. The importer still explicitly reports
the two skipped multi-handler bodies. Recovery budgets 0 and 64 preserve the
CFG and name exhaustion. A deliberately wrong state source is executed through
the same native pipeline and rejected against the correct oracle.

The implicit-null-property refusal also pins an existing interpreter/Node
difference: Node enters the catch and returns 42, while the interpreter returns
undefined. Its report records both results separately. This case never becomes
native and is not used as an admitted-result equivalence claim.

[The source IR test](../test/CTJS/IR/exceptions.mlir) checks region round-trips,
numeric type flow and eight verifier refusals.
[The C++ target test](../test/Target/Cpp/native-exceptions.mlir) executes explicit,
deduced and hoisted output with both compilers. It covers nested target throws,
negative zero, unused catch values, foreign exception propagation, writable
opaque-ABI uses of a copied catch binding, name collisions and nine malformed
IR controls. Nested target emission is not nested-handler JavaScript admission.
The existing handler-structuring regression also passes.

The final full devbox gate passes **461/461 CTests**, including **151/151 lit
cases**, in **539.89 seconds**. All **553 C++ files** pass formatting; the new
printer include and whitespace checks pass separately. Default and disabled
optimization coverage floors retain Bootstrap **19/574**, p5 **39/4754** and
Phaser **45/7725**, including their existing closure-lifting counts. The exact
CommonJS/browser/realm-fallback Data probes still admit **0/7 native** each;
exception support does not establish their remaining effect/ownership proofs.

## Validation before expanding scope

Add a closed throwing callee, an assignment whose right-hand side throws and an
uncaught primitive boundary. Check source/Node/interpreter agreement and the
exact source denominator and named refusals, not just emitted syntax.

As payload support expands, test null versus undefined, booleans versus numbers,
NaN/negative zero, long-string ownership after stack churn, saved catch values,
and object alias identity. Use ASan/UBSan and leak checks for owning payloads
and cleanup; reject borrowed frame values and unsupported mixed payloads.

Before claiming finally, require return preserved/overridden by finally,
throw preserved/replaced, throw from catch, nested finally order, and both break
and continue crossing finally. Include a finally that throws during an existing
JS throw without terminating C++, and a return from finally that suppresses a
pending JS throw. Keep foreign-call, reentry, suspension and runtime-failure
controls separate. Existing boxed exception/status regressions must continue
to pass throughout the native work.
