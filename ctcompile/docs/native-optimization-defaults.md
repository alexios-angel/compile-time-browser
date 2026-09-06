# Native optimization defaults

`--ctnative-lower-to-emitc` runs primitive precomputation followed by private
reachability pruning by default. Both run after the caller's global resolution
and control-flow structuring, before native closure lifting and type inference.
The native entry owns this policy, so direct command-line and C++ pass clients
receive the same defaults as `test/native-pipeline.cmake`.

Precomputation replaces proved primitive expressions and selects known
structured branches. Runtime producers, including effectful calls, remain at
their original positions. Reachability then removes private definitions with
no path from an entry, export, symbolic reference or numeric closure reference.
This order allows a selected branch to make a helper unreachable before native
admission. Unknown references, unsupported symbol scopes or exhausted graph
work retain all functions. Existing native type, ownership and effect proofs
still decide admission.

These passes do not clone functions or unroll loops. Each has its existing
100,000-step default analysis budget. A precomputation limit leaves remaining
expressions unchanged; a reachability limit retains the complete function set.
Bookkeeping walks and pattern setup are additional work proportional to the IR,
so the step counters are analysis limits, not wall-clock or memory guarantees.
Input proof annotations never bypass either analysis.

Specialization, heap partial evaluation, supercompilation and snapshot
deforestation remain explicit opt-ins. The boxed pipeline is unchanged.
Native closure lifting and the ordinary EmitC cleanup stages remain required
parts of lowering and are independent of this optimization policy.

| Native lowering option | Default | Effect |
| --- | --- | --- |
| `optimize` | `true` | Enable the two default stages |
| `precompute` | `true` | Enable primitive precomputation |
| `prune-unreachable` | `true` | Enable private reachability pruning |
| `precompute-max-steps` | `100000` | Bound symbolic analysis and rewrites |
| `reachability-max-steps` | `100000` | Bound graph discovery |
| `optimization-report` | `false` | Print both stages' counters |

Use `--ctnative-lower-to-emitc=optimize=false` for the previous native lowering
behavior. Use `precompute=false` or `prune-unreachable=false` to disable one
stage. Setting both individually to false is equivalent to `optimize=false`.
For example:

```sh
ctjs-translate --ctbrowser-js-to-ctjs input.js |
  ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
    '--ctnative-lower-to-emitc=optimization-report=true'

ctjs-translate --ctbrowser-js-to-ctjs input.js |
  ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
    '--ctnative-lower-to-emitc=optimize=false'
```

An explicit standalone pass still runs when defaults are disabled. For example,
`--ctnative-precompute --ctnative-lower-to-emitc=optimize=false` precomputes once
and then lowers. This permits reproducible experiments with individual stages.
Running an explicit stage without disabling its corresponding default invokes
it twice; diagnostics are rederived on each invocation.

`test/native-pipeline.cmake` accepts `-DOPTIMIZE=OFF` for the disabled baseline,
`-DPRECOMPUTE=OFF` and `-DPRUNE_UNREACHABLE=OFF` for individual opt-outs, and
`-DOPTIMIZATION_REPORT=ON` for counters. Undefined options inherit the native
entry's defaults. Existing explicit `PRECOMPUTE=ON` and `PRUNE_UNREACHABLE=ON`
stages retain their order and nonzero-change checks, with their duplicate
internal stage disabled. Explicit stages still run with `OPTIMIZE=OFF`.
`PRECOMPUTE_MAX_STEPS` and `REACHABILITY_MAX_STEPS` configure only the internal
default stages. The CMake fixture helper's `NO_DEFAULT_OPTIMIZATIONS` option
selects the disabled baseline.

`test/CTNative/default-optimizations.mlir` checks the native entry's defaults,
stage order, individual and combined opt-outs, zero budgets and diagnostics.
`test/check-native-optimization-defaults.cmake` builds the same JavaScript with
defaults enabled and disabled, checks generated C++ actually differs, and runs
the existing standalone compilation, no-VM-symbol and interpreter comparisons
for both. Its eight fixed observations include call order, NaN, signed zero
and strings. Historical tests that inspect unsimplified operation shapes select
the disabled baseline explicitly; those checks retain their original purpose.

On the devbox, the default-policy fixture folds 17 expressions and three
branches in 294 symbolic steps without exhaustion. Both variants produce all
eight expected numeric observations and contain no ctbrowser symbols. Generated
C++ decreases from 7,840 to 7,043 bytes (10.2%) with the
[readable literal policy](native-literals.md), [source names](native-source-names.md)
and [const bindings](native-const-bindings.md), after the callee-before-argument
bytecode correction. These replace the preceding 7,366/6,701-byte measurements;
the optimization stages and expected observations are unchanged. The combined
enabled/disabled differential and wrapper-option checks
include native C++ compilation and interpreter comparisons.
