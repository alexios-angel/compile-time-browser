# Iterator method breaks and fixed-bit mask strides, 2026-09-22

Resumed clean `ccbdf5d6` from the `loop-captured-break` boundary recorded in
HANDOFF, plan 00 and the iteration 51 journal. Iteration 52 was interrupted
before its first build; its two production edits and source draft were preserved
and finished. The previous subagents were no longer live; the same bounded
source/raw/escape tasks resumed under their original claim owners.
`codex-wip-20260907` was absent; other branches were preserved. Linux process
identities and Windows CIM showed no Claude executable, CLI or loop. No browser,
shared implementation or runtime-oracle semantics changed.

## Landed

**dba0df1c** reuses complete continuation normalization before iterator-method
state analysis. Pure loop exit projections can have zero results, retaining exact
tags, complete selector-use checks, pure arms, live-value and budget proofs.
Receiver/capture state follows the existing scalar transport. Existing tuple and
arithmetic cleanup now also visits proved mutable methods, removing unused
completion tags without dropping source effects or live counters. Method FuncOps
are saved before replacing entry operations. A method aliasing the entry is
refused before normalization can invalidate cached operations. DOM preparation
still publishes only after the complete cloned module is proved.

The original `loop-captured-break` source executes byte-identically. Two new
sources check paired receiver/captured state, ordered updates before break,
skipped suffixes, ordinary local counters, exhaustion and latest close state.
All 74 earlier source bodies remain; the driver has 21 positives and 61 refusal
sources. Raw controls cover exact state edges, effects and close arguments,
malformed correspondence, observed/unknown tags, effectful empty dispatch,
poison, entry aliasing and budget cuts. Generated code uses typed scalars and
public DOM calls, with no boxed iterator state, GC or Script dependency.

**4e3acdc9** preserves full strides when AND/OR/XOR masks change only proved
fixed input bits within one signed conversion band. It reuses the existing
fixed-bit interval bound and endpoint evaluator. Conversion discontinuities,
complete reload/store census and budgets retain their previous conservative
behavior. Twenty-one source witnesses extend the unchanged 96 OR/XOR and 67 AND
sources. Paired CFG/SCF rows cover operand order, signed inputs, retained children,
actual reload overlap, later stores and masks that change varying bits.

## Focused validation

| Check | Result |
| --- | --- |
| Exact escape arrays CTest | 1/1 PASS; 1.84 s test / 1.85 s total |
| AND and OR/XOR lit | 2/2 PASS; 0.15 s total |
| Custom iteration lit | 1/1 PASS; 528.43 s total; 336 native executions, 626 refusals, 168 Node/VM observations |
| Final exact host CTest | 1/1 PASS; 0.87 s test / 0.88 s total |
| Final three-source subset | PASS; 48 native executions, 84 refusals, 24 Node/VM observations |

The initial explicit build passed. The first host run found a stale slot-operation
pointer in the new cleanup setup after entry replacement and crashed. Caching
method FuncOps before replacement fixed it; the next host run passed (1.13 s
total), followed by three admission preflights and the custom lit case.
Independent review then identified malformed entry self-aliasing. Its guard and
raw regression passed the final host gate and the three new source cases.

The complete custom case used the implementation before the entry-alias guard.
The final subset uses the final compiler and reruns only the three new positives
and six new source refusals. All 24 final emitted C++ files match the full-case
outputs after replacing only their work-directory prefixes in source diagnostics
and type pins. All nine final code/test hashes match local and devbox files.
The five escape files were committed after their initial passing gate; their
checks were not repeated. No full custom replay is claimed after the guard.

Initial explicit targets through `tools/remote-build.sh`:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

The native correction rebuilt the five native targets; the final guard rebuilt
only `ctjs-opt ctcompile-test-host-contract`. Every devbox command held
`/tmp/ctbrowser-devbox-build.lock`. Exact checks from the synced repository root:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-custom-iteration[.]test$'
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-iteration52-final-subset.py
```

The retained subset runner uses the existing driver and generated lit tool paths;
it changes only the selected positive/refusal lists. Exact runners, logs, lit JSON,
source hashes, original/new sources and C++ examples are at
`../test-results/2026-09-22-iterator-method-break/` beside the monorepo.

Required `tools/format.sh --check` retains 16 pre-existing diagnostics in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp` (the latter three under
`ctcompile/lib/CTNative`). Changed C++ passes pinned clang-format; Python passes
Black/AST; whitespace and shell runner syntax checks pass. Agent checks passed
82 source syntax checks, 84 exact Node traces, all 184 escape source calls and
21 new exact escape observations.

No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
additional sanitizer, local build or push ran. Unchanged nested/dataset lit was
not replayed. Historical measurements remain historical. The devbox idle timer
was restored and verified active/enabled.

## Exact next boundary

Start with `refusals()["loop-break-external-captured-read"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. It preserves
the ordered method-break source and adds `return count + emitted`, observing
the captured cell outside the iterator protocol. Both optimization policies
refuse with `native DOM source: DOM iterator capture cell has an external reader
or writer`. Prove how the latest scalar cell remains observable after iteration
before widening the confinement boundary.

The source driver does not retain successful refusal stderr. Each policy was
replayed once solely to save its exact diagnostic, returning status 1. The
capture runner and diagnostics are in the artifacts; no native execution is
claimed for this source.

Nested closures/custom opens, body return/throw close behavior, literal range-for
printing, unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted; no vendor coverage gain is claimed. Richer bitwise
range unions remain separate.
