# Enclosing iterator capture state and XOR complements, 2026-09-22

Continued clean `e28b03b8` from the `loop-break-external-captured-read` boundary
in HANDOFF, plan 00 and the iteration 52 completion journal. That session had
already finished its interrupted drafts. There was no uncommitted predecessor
work; `codex-wip-20260907` was absent and other branches were preserved.
Linux `/proc` inspection found no matches among 14 readable executables but
permission errors prevented a complete check. Windows CIM inspected 342 processes
and found no Claude executable/CLI/loop. Claude status remained uncertain;
concurrent-agent ownership rules applied. No browser, shared implementation or
runtime-oracle semantics changed.

## Landed

**35f59014** extends the existing captured-state tuple into the enclosing
entry. Direct reads select the current scalar; assignments update it in source
order through branches, loops and importer completion selections. An Undefined
cell requires its first root assignment to supply a literal Number before every
capturing method and observation; subsequent writes retain their source position.
The existing MLIR entry verifier checks region correspondence before rewriting,
and dominance analysis proves allocation and initialization order. Other closures,
cell aliases and escapes remain refused. The close branch now yields updated
state from `return`, or unchanged final `next` state when exhausted. Complete
DOM/type/lifetime analysis still precedes publication.

Six previous refusal sources are promoted byte-identically, including the saved
method-break/post-loop reader. One new two-cell program checks entry-loop and
conditional body updates, latest close state, ordered post-loop writes and fresh
invocations. All 82 earlier source bodies and existing literal native/lifetime
checks remain. Raw tests cover state ordering, zero-body traversal, initial reads,
aliases, malformed loop correspondence and budget cuts. Generated code uses
typed scalars and public DOM calls without boxed cells, GC or Script dependencies.

**c2254fc8** uses the existing fixed-bit mask proof to recognize XOR reversal
when every varying bit flips. The affine endpoint path preserves the full stride
within one signed conversion band. Conversion discontinuities, reload overlap,
later stores and proof budgets keep their previous conservative behavior.
Fourteen CFG and fourteen SCF rows plus fifteen complete sources extend the
unchanged earlier tests; all 108 earlier OR/XOR bodies/calls/checks remain.

## Focused validation

| Check | Result |
| --- | --- |
| Exact escape arrays CTest | 1/1 PASS; 1.67 s test / 1.68 s total |
| AND and OR/XOR lit | 2/2 PASS; 0.14 s |
| Final exact host CTest | 1/1 PASS; 0.95 s test / 0.96 s total |
| Final seven new admission preflights | PASS |
| Custom iteration lit | 1/1 PASS; 745.61 s; 448 native executions, 790 refusals, 224 Node/VM observations |

The initial explicit build and escape checks passed. The first native host gate
failed four copies of one new structural assertion. An IR dump showed that
existing completion normalization duplicates final arithmetic into both close
arms and joins the complete return. The corrected assertion checks both arms,
the unchanged ordinary result prefix, exhausted state and ordered return writes.
The temporary dump was removed. A new combined source initially failed because
a narrow initialization-order helper did not cross imported IndexSwitch regions;
existing MLIR dominance analysis fixed it. All seven new source preflights then
passed. The final host and custom lit use the final compiler and tests; all seven
final code/test hashes match local and devbox files. Escape checks were not
repeated after their separate commit.

Initial explicit targets through `tools/remote-build.sh`:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

The dominance/debug build selected `ctjs-opt ctcompile-test-host-contract`;
the final assertion correction rebuilt only `ctcompile-test-host-contract`.
Every devbox command held `/tmp/ctbrowser-devbox-build.lock`. Exact checks:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-iteration53-preflight.py
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-custom-iteration[.]test$'
```

Required `tools/format.sh --check` retains 16 pre-existing diagnostics in untouched
`ctbrowser/tools/ctdrive/ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp` (the latter three under
`ctcompile/lib/CTNative`). Changed C++ passes pinned clang-format; Python passes
Black/AST; whitespace and temporary shell runner syntax checks pass. Source-agent
checks passed 87 Node syntax checks and 280 exact Node observations; the final
new observed non-Number refusal received another syntax check. Escape-agent
checks executed all 123 source functions and checked fifteen new exact outputs.

No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
additional sanitizer, local build or push ran. Unchanged nested/dataset lit was
not replayed. The devbox idle timer was restored and verified active/enabled.
Runners, logs, hashes, lit JSON, source/IR witnesses and representative emitted
C++ are at `../test-results/2026-09-22-iterator-entry-state/` beside the monorepo.

## Exact next boundary

Start with `refusals()["entry-captured-sibling-reader"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. It retains the
new ordered two-cell source, adds `const read = () => emitted` after iteration,
and returns `count + read() + closed`. Both policies refuse with
`native DOM source: DOM iterator capture cell has an external reader or writer`.
Prove that this sibling closure reads the latest shared scalar cell before
relaxing the complete capture census. The driver does not save successful refusal
stderr; each policy was replayed once solely to retain its diagnostic (status 1),
with no native execution claimed for that source.

Nested custom opens, body return/throw close behavior, literal range-for printing,
unguarded Bootstrap defaults and the application driver remain open. Full
Bootstrap is not admitted; no vendor coverage gain is claimed. Richer bitwise
range unions remain separate.
