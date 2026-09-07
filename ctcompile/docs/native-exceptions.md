# Native JavaScript exceptions using C++ unwinding

**Status: design and measured prerequisites only.** Native lowering does not
yet emit JavaScript throws or catches as C++ exceptions. C++ `throw` and typed
`catch` can implement the unwinding mechanism, provided the compiler first
preserves JavaScript values, exceptional control flow and completion semantics.
This work is separate from [private provider mutation summaries](native-provider-mutations.md).

`console.error(...)` is an ordinary call, not a JavaScript `throw`. Its callback
can return, throw, mutate publication, change Maps or reenter the program.
Adding C++ exception support supplies neither its effect summary nor permission
to carry private Map facts across it. The exact Bootstrap conflict path keeps
its existing provider/reentry boundary.

## What is present today

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
interpreted callers. Native CFG structuring currently rejects the effectful
`push_handler` and `check` terminators; the existing
[handler structuring regression](../test/Lowering/EmitC/lift-to-scf-handlers.mlir)
records both obstacles. Declaring them pure would allow invalid reordering.

## Smallest useful native target

Start with one nonempty catch and no finally, nested handler, suspension or
reentry. Require closed native calls and proved primitive thrown values. Keep
unsupported input refused by name. A useful initial gate is the measured
`single_catch.js` specimen:

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

The acceptance target is **2/2 native**, `caught42=42` and `normal20=20` under
both compilers, with no VM symbols. It is currently 0/2 native. This tests the
exceptional state transfer: restoring `mark` from try entry incorrectly yields
32. The normal invocation also prevents an implementation from treating every
entry into a protected region as a throw.

The implementation needs three pieces before this can be claimed:

1. A bounded, live exception-region recovery pass before generic CFG-to-SCF.
   Recognize one balanced protected region, its actual throw/check edges,
   handler-only landing and normal exits. Recover a structured native try/catch
   operation with explicit normal results and catch-visible state. Reject
   unsupported exits or incomplete recovery atomically. Report attributes do
   not authorize recovery.
2. Exception-aware type/effect admission. Infer the catch payload from every
   possible explicit throw and transitively admitted throwing callee; distinguish
   normal returns from exceptional exits. An always-throwing function needs a
   bottom normal-result fact instead of inventing a boxed successful result.
   Unknown calls or unknown thrown payloads must not silently become primitive.
3. Typed native lowering and C++ printer support for those structured regions,
   a terminal throw operation and owning payload construction. Preserve scoped
   declarations and source evaluation order. A typed operation is preferable to
   stitching variable-bearing try/catch text into `emitc.verbatim`.

Catch-visible variables require storage whose lifetime spans both regions.
Materialize only the values carried on exceptional edges and update them at
the corresponding source points. In particular, save the pre-call value before
a throwing assignment's right-hand side, and publish the assigned value only
on normal return. Ordinary SSA temporaries may keep their const/constexpr
spelling; the exceptional state slots are mutable. The new operations must
participate in effects, liveness, type inference and declaration placement.

This is a small acceptance target with a real IR prerequisite. A throw-printer
hook alone does not deliver it. A primitive throw-only leaf can be a printer
prerequisite, but must not be reported as native try/catch support.

## Owning thrown values

Use a dedicated exception type, conceptually:

```cpp
using thrown_value = std::variant<undefined_t, null_t, bool, js_num, std::string>;
struct js_exception { thrown_value value; };
```

These are proposed names and types, not an existing runtime API. Initial
admission may start with numbers, but the representation must keep JavaScript
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
rethrows must land together. Until then, finally/nested-handler paths remain
outside native exception admission.

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

Initially permit propagation only within the admitted native C++ call component.
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

## Validation before expanding scope

Require source/interpreter/Node agreement and explicit/deduced standalone C++
under GCC and Clang for the initial two-function gate. Add a closed throwing
callee, two distinct throw sites with different live local state, an assignment
whose right-hand side throws, normal return and an uncaught primitive boundary.
Check the exact source denominator and named refusals, not just emitted syntax.

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
