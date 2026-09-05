# Native callback arguments and Bootstrap startup

The native lowering can prove the target of a capture-free function argument
when every caller of its wrapper supplies the same target. For example:

```js
var startup = (function initialize(factory) {
    return factory();
})(function bootstrapFactory() { return 42; });
```

The generated C++ contains an ordinary `initialize()` calling
`bootstrapFactory()`. Neither function takes a runtime callback object.

## Proof and transformation

`closureLifter::specializeCallbacks` runs inside `--ctnative-lower-to-emitc`,
before the existing closure and cell censuses. It does not change the importer
or the boxed pipeline.

Every creation of the wrapper must be used only as the callee of direct calls
to that wrapper. Every such call must pass the wrapper's actual closure.
Wrappers that expose their own closure, read `arguments` or rest parameters,
or carry `pass_new_target` are excluded.

At each candidate parameter, every caller must supply a directly created,
capture-free closure with the same function target. The existing lift checks
also exclude lexical receivers that this tier cannot preserve. No function
identity is inferred from one caller while ignoring another.

Calls through a proved parameter become `ctjs.call_direct`, retaining the
parameter as their callee value. Receivers and argument evaluation order stay
unchanged. Surplus arguments are not rewritten. Missing arguments are padded
only when the target does not inspect its raw argument window.

Parameter erasure requires a stronger proof: every use is a supported call,
every supplied closure is used only at that wrapper parameter, no other
creation of the callback target exists, and the callback's own identity does
not escape. The closure is then recreated inside the wrapper for the existing
lift, and the original closures and parameter are removed together. The
source-level function bodies remain in the module; this does not discard
functions from the admission denominator.

Escaping values remain present and receive `ctnative.callback_refusal`.
Knowing which body a function enters is sufficient to name its calls, but
does not justify discarding an observable function object.

Candidate wrappers are collected once, before the rewrites. A callback call
that exposes another wrapper does not recursively specialize that wrapper in
this step. Passing the same callback target in multiple parameter positions
also conservatively retains those parameters. Neither limitation licenses a
boxed fallback.

## Measured Bootstrap result

Against the `76c0226` baseline:

| Measurement | Before | After |
|---|---:|---:|
| Native functions admitted | 4/574 | 4/574 |
| Resolver cohort claimed / lifted / refused | 0 / 5 / 19 | 0 / 5 / 19 |
| Calls rewritten during native lowering | 206 | 208 |
| Remaining direct-call sites after native lowering | 222 | 224 |
| Distinct targets of those sites | 62 | 63 |
| Functions on direct-call paths from entry, including entry | 2 | 6 |

The two newly named calls are the CommonJS and browser invocations of the
factory `fn$2` inside the UMD wrapper `fn$1`. The AMD `define(factory)` call
still exposes the function value, so the factory parameter is not erased and
the bundle remains refused. No environment branch is assumed away.

The entry graph now includes `_script_$0`, `fn$1`, `fn$2`, `bi$88`, `fn$125`
and `fn$178`. These are static paths, not evidence that every branch executes.
Bootstrap still has **zero additional admitted native functions**. Its host
environment, factory-owned `Map`, method tables, exports and callbacks remain
work ahead.

## Verification

`native-callback-fixture.js` admits 21/21 functions. Its eight observable
results cover both branches, repeated calls, argument side effects, missing
arguments, multiple callback parameters and loops. The standard pipeline
compares the executable with the interpreter, checks for VM symbols, compiles
with GCC and Clang under `-Werror -Wconversion`, and verifies deduced type pins.

`callback-arguments.mlir` checks refusal of mixed callback targets, captures,
raw argument access, unknown wrapper callers, lexical receivers, forwarded
`new.target` and escaping callback identities, including a callback returning
its own function object.
`callback-startup.test` requires a direct path from the actual bundle entry
to its factory and proves that an existing but unreachable function cannot
satisfy that requirement. It also checks the graph after EmitC symbol renaming.

Removing the target-agreement, raw-argument and closed-caller guards one at a
time was verified to fail `MIXED`, `RAW` and `OPEN`, respectively, after each
mutant was rebuilt and its source checked on the devbox. Other native refusals
still prevented those mutants from emitting complete programs; these are
proofs that the specific diagnostics are exercised, not demonstrated wrong
answers. The capture-guard removal experiment was blocked by automatic approval
review before building; that guard was restored and its normal refusal test
remains covered.

At the callback checkpoint, before the subsequent string-carrier changes,
the devbox full build and suite passed **265/265**, up from the reproduced
**258/258** baseline. The additional lexical-receiver, self-identity and
`new.target` cases also pass in a subsequent full lit run. Bootstrap's exact
native-lift floor is now 208; a separate 209-floor check fails with the expected
208-call diagnostic. The p5 and phaser admission and lift counts remain
37/4754 and 328, and 43/7725 and 283, respectively. The repository formatting
gate and `git diff --check` pass.

Regenerated boxed Bootstrap C++ is unchanged from the recorded baseline:
10,976,150 bytes, SHA-256
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba`.

Reproduce with `tools/check/named-callees.py --translate <ctjs-translate>
--opt <ctjs-opt> --corpus ctbrowser/vendor/bootstrap/bootstrap.bundle.js
--require-entry-callee 'fn$2'`. The original 19-function report is explicitly
a resolver-stage cohort; the second report includes the later native stage.

## Next initialization milestone

Owning strings and primitive-key Maps across closed calls are now available; see
[native-strings.md](native-strings.md) and [native-maps.md](native-maps.md).
Map handles survive factory return and can be shared through lifted captures.
Returned callable method tables, nested Maps and object keys still need support.

Compile a source-derived initialization slice using Bootstrap's unchanged UMD
wrapper and its Map-backed Data object, then exercise exported `set`, `get`
and `remove` after the factory returns. This is deliberately not a claim that
the complete factory compiles. Cover CommonJS publication, browser publication
and delayed AMD factory invocation rather than assuming a branch is dead.

The slice needs an owned generated module structure for persistent captured
state, native callable exports, typed host publication, native strings and a
typed Map preserving JavaScript key identity and insertion order. Existing
frame-local capture pointers cannot be allowed to escape the factory, and a
plain `std::unordered_map` is not a semantics-preserving substitute. Compare
the publication and callback traces with the interpreter, including conflicts,
shared state and destruction, without linking the VM or collector.

That architecture can then carry the real Alert registration and readiness
callbacks through initialization. Merely registering a callback whose body
still cannot compile is not a completed native startup path.
